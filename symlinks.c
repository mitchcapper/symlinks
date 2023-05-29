#include "config.h"
#include <unistd.h>
#include "filename.h"

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>

#if !defined(__APPLE__)
#include <malloc.h>
#endif

#include <string.h>
#include <fcntl.h>
#ifndef _WIN32
#include <sys/param.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <stddef.h>
#include <errno.h>

#ifndef S_ISLNK
#define S_ISLNK(mode) (((mode) & (_S_IFMT)) == (_S_IFLNK))
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifdef _WIN32
#include <ctype.h>
	#define STRSTR stristr
	#define STRCMP stricmp
	#define STRNCMP strnicmp
	#define CHAR_CMP(c1, c2) ( tolower(c1) == tolower(c2) )
#else
	#define STRSTR strstr
	#define STRCMP strcmp
	#define STRNCMP strncmp
	#define CHAR_CMP(c1, c2) (c1 == c2)
#endif

#define progver "%s: scan/change symbolic links - v1.5.0\n\n"
static const char * StrStrAllSlash(const char * haystack, const char * needle);

static char *progname;
static int verbose = 0,
           fix_links = 0,
           recurse = 0,
           delete = 0,
           shorten = 0,
           testing = 0,
           single_fs = 1;

/*
 * tidypath removes excess slashes and "." references from a path string
 */

static bool CONTINUE_ON_ERROR = true;
static void have_error(const char* str) {
	perror(str);
	if (!CONTINUE_ON_ERROR)
		exit(1);
}

static int substr(char *s, char *old, char *new) {
    char *tmp = NULL;
    unsigned long oldlen = strlen(old), newlen = 0;

    if (NULL == StrStrAllSlash(s, old)) {
        return 0;
    }

    if (new) {
        newlen = strlen(new);
    }

    if (newlen > oldlen) {
        if ((tmp = malloc(strlen(s))) == NULL) {
            fprintf(stderr, "no memory\n");
            exit(1);
        }
    }

    while (NULL != (s = StrStrAllSlash(s, old))) {
        char *p, *old_s = s;

        if (new) {
            if (newlen > oldlen) {
                old_s = strcpy(tmp, s);
            }

            p = new;

            while (*p) {
                *s++ = *p++;
            }
        }

        p = old_s + oldlen;

        while ((*s++ = *p++));
    }

    if (tmp) {
        free(tmp);
    }

    return 1;
}
char DIR_SEPARATOR_STR[2];
static const char * get_dir_separator_as_str(){

    DIR_SEPARATOR_STR[0] = DIR_SEPARATOR;
    DIR_SEPARATOR_STR[1] = 0;
    return DIR_SEPARATOR_STR;
}

//like strstr but any slashes will be matched against all path chars
static const char * StrStrAllSlash(const char * haystack, const char * needle){
#ifndef _WIN32
    return STRSTR(haystack,needle);
#else
    if (strlen(needle) > strlen(haystack))
        return NULL;

    char firstChar[10];
    strcpy_s(firstChar,sizeof(firstChar),needle);
    if (ISSLASH(needle[0]))
        strcpy_s(firstChar,sizeof(firstChar),SLASHES);
    else
        strncpy_s(firstChar,sizeof(firstChar),needle,1);

    const char * pos = haystack;
    const char * match;
    bool is_match = 0;
    while ( *pos && (match = strpbrk(pos,firstChar)) != NULL && strlen(match) >= strlen(needle)){
        is_match=true;
        for (int i = 1; i < strlen(needle) && is_match;i++){
            if (ISSLASH(needle[i])){
                if (! ISSLASH(match[i]))
                    is_match = false;
            }else if (match[i] != needle[i])
                is_match = false;
        }
        if (is_match)
            return match;

        pos = match + 1;
    }
    return NULL;

    #endif
}
static int tidy_path(char *path) {
    int tidied = 0;
    char *s, *p;
    s = path + strlen(path) - 1;
    const char * DIR_SEPARATOR_STR = get_dir_separator_as_str();
    if (! ISSLASH(s[0])) {  /* tmp trailing slash simplifies things */
        s[1] = DIR_SEPARATOR;
        s[2] = '\0';
    }

    while (substr(path, "/./", "/")) {
        tidied = 1;
    }

    while (substr(path, "//", "/")) {
        tidied = 1;
    }

    while ((p = StrStrAllSlash(path, "/../")) != NULL) {
        s = p + 3;

        for (p--; p != path; p--) {
            if (ISSLASH(*p)) { break; }
        }

        if (! ISSLASH(*p)) { break; }

        while ((*p++ = *s++));

        tidied = 1;
    }

    if (*path == '\0') {
        strcpy(path, DIR_SEPARATOR_STR);
    }

    p = path + strlen(path) - 1;

    if (p != path && ISSLASH( *p ) ) {
        *p-- = '\0';  /* remove tmp trailing slash */
    }

    while (p != path && ISSLASH(*p)) {  /* remove any others */
        *p-- = '\0';
        tidied = 1;
    }

    while (strlen(path) >= 2 && path[0] == '.' && ISSLASH(path[1])) {
        for (p = path, s = path + 2; (*p++ = *s++););

        tidied = 1;
    }

    return tidied;
}


static int shorten_path(char *path, char *abspath) {
    static char dir[PATH_MAX];
    int shortened = 0;
    char *p;

    /* get rid of unnecessary "../dir" sequences */
    while (abspath && strlen(abspath) > 1 && (p = StrStrAllSlash(path, "../"))) {
        /* find innermost occurance of "../dir", and save "dir" */
        int slashes = 2;
        char *a, *s, *d = dir;

        while ((s = StrStrAllSlash(p + 3, "../"))) {
            ++slashes;
            p = s;
        }

        s = p + 3;
        *d++ = DIR_SEPARATOR;

        while (*s && ! ISSLASH(*s)) {
            *d++ = *s++;
        }

        *d++ = DIR_SEPARATOR;
        *d = '\0';

        if (strlen(dir) == 2 && ISSLASH( dir[0] ) &&  ISSLASH( dir[1] ) ) {
            break;
        }

        /* note: p still points at ../dir */
        if (! ISSLASH(*s) || !*++s) {
            break;
        }

        a = abspath + strlen(abspath) - 1;

        while (slashes-- > 0) {
            if (a <= abspath) {
                goto ughh;
            }

            while ( (--a) && ! ISSLASH(*a) ) {
                if (a <= abspath) {
                    goto ughh;
                }
            }
        }

        if (STRNCMP(dir, a, strlen(dir))) {
            break;
        }

        while ((*p++ = *s++));  /* delete the ../dir */

        shortened = 1;
    }

ughh:
    return shortened;
}


static void fix_symlink(char *path, dev_t my_dev) {
    const char * DIR_SEPARATOR_STR = get_dir_separator_as_str();
    static char lpath[PATH_MAX], new[PATH_MAX], abspath[PATH_MAX];
    char *p, *np, *lp, *tail, *msg;
    struct stat stbuf, lstbuf;
    int fix_abs = 0, fix_messy = 0, fix_long = 0;
    size_t c;

    if ((c = readlink(path, lpath, sizeof(lpath))) == -1) {
		have_error(path);
        return;
    }

    lpath[c] = '\0';  /* readlink does not null terminate it */
    /* construct the absolute address of the link */
    abspath[0] = '\0';

    if ( ! IS_ABSOLUTE_PATH( lpath) ) {
        strcat(abspath, path);
        c = strlen(abspath);

        if ( (c > 0) && ISSLASH(abspath[c - 1])) {
            abspath[c - 1] = '\0';    /* cut trailing / */
        }

        if ((p = LAST_SLASH_IN_PATH(abspath)) != NULL) {
            *p = '\0';    /* cut last component */
        }

        strcat(abspath, DIR_SEPARATOR_STR);
    }

    strcat(abspath, lpath);
    (void) tidy_path(abspath);

    /* check for various things */
    if (stat(abspath, &stbuf) == -1) {
        printf("dangling: %s -> %s\n", path, lpath);

        if (delete) {
            if (unlink(path)) {
				have_error(path);
            } else {
                printf("deleted:  %s -> %s\n", path, lpath);
            }
        }

        return;
    }

    if (single_fs) {
        lstat(abspath, &lstbuf);  /* if the above didn't fail, then this shouldn't */
    }

    if (single_fs && lstbuf.st_dev != my_dev) {
        msg = "other_fs:";
    } else if (IS_ABSOLUTE_PATH(lpath)) {
        msg = "absolute:";
        fix_abs = 1;
    } else if (verbose) {
        msg = "relative:";
    } else {
        msg = NULL;
    }

    fix_messy = tidy_path(strcpy(new, lpath));

    if (shorten) {
        fix_long = shorten_path(new, path);
    }

    if (!fix_abs) {
        if (fix_messy) {
            msg = "messy:   ";
        } else if (fix_long) {
            msg = "lengthy: ";
        }
    }

    if (msg != NULL) {
        printf("%s %s -> %s\n", msg, path, lpath);
    }

    if (!(fix_links || testing) || !(fix_messy || fix_abs || fix_long)) {
        return;
    }

    if (fix_abs) {
        /* convert an absolute link to relative: */
        /* point tail at first part of lpath that differs from path */
        /* point p    at first part of path  that differs from lpath */
        (void) tidy_path(lpath);

        tail = lp = lpath;
        p = np = path;

		while (*p && (CHAR_CMP(*p, *tail) || (ISSLASH(*p) && ISSLASH(*tail)))) {
			p++;
			tail++;
		}

        /* now create new, with "../"s followed by tail */
        np = new;

        while (*p) {
            if (p++ && ISSLASH( *p ) ) {
                *np++ = '.';
                *np++ = '.';
                *np++ = DIR_SEPARATOR;

                while ( ISSLASH( *p ) ) {
                    ++p;
                }
            }
        }

        strcpy(np, tail);
        (void) tidy_path(new);

        if (shorten) {
            (void) shorten_path(new, path);
        }
    }

    shorten_path(new, path);

    if (!testing) {
        if (unlink(path)) {
			have_error(path);
            return;
        }

        if (symlink(new, path)) {
			char buffer[1024];
			snprintf(buffer, sizeof(buffer), "Warning deleted existing symlink but unable to create new one: %s => %s", new,path);
			have_error(buffer);
            return;
        }
    }

    printf("changed:  %s -> %s\n", path, new);
}

static void dirwalk(char *path, unsigned long pathlen, dev_t dev) {
    char *name;
    DIR *dfd;
    static struct stat st;
    static struct dirent *dp;

    if ((dfd = opendir(path)) == NULL) {
		have_error(path);
        return;
    }

    name = path + pathlen;

    if ( ! ISSLASH( *(name - 1) ) ) {
        *name++ = DIR_SEPARATOR;
    }

    while ((dp = readdir(dfd)) != NULL) {
        strcpy(name, dp->d_name);

        if (STRCMP(name, ".") && STRCMP(name, "..")) {
            if (lstat(path, &st) == -1) {
				have_error(path);
            } else if (st.st_dev == dev) {
                if (S_ISLNK(st.st_mode)) {
                    fix_symlink(path, dev);
                } else if (recurse && S_ISDIR(st.st_mode)) {
                    dirwalk(path, strlen(path), dev);
                }
            }
        }
    }

    closedir(dfd);
    path[pathlen] = '\0';
}

static void usage_error(void) {
    fprintf(stderr, progver, progname);
    fprintf(stderr, "Usage:\t%s [-cedorstv] dirlist\n\n", progname);
    fprintf(stderr, "Flags:"
            "\t-c  change absolute/messy links to relative\n"
            "\t-d  delete dangling links\n"
			"\t-e  exit on error (rather than try next)\n"
            "\t-o  warn about links across file systems\n"
            "\t-r  recurse into subdirs\n"
            "\t-s  shorten lengthy links (displayed in output only when -c not specified)\n"
            "\t-t  show what would be done by -c\n"
            "\t-v  verbose (show all symlinks)\n\n");
    exit(1);
}

int main(int argc, char **argv) {
    static char path[PATH_MAX + 2], cwd[PATH_MAX + 2];
    int dircount = 0;
    char c, *p;
    const char * DIR_SEPARATOR_STR = get_dir_separator_as_str();

    if ((progname = (char *) LAST_SLASH_IN_PATH(*argv)) == NULL) {
        progname = *argv;
    } else {
        progname++;
    }

    if (NULL == getcwd(cwd, PATH_MAX)) {
        fprintf(stderr, "getcwd() failed\n");
        exit(1);
    }

    if (!*cwd || ! ISSLASH(cwd[strlen(cwd) - 1])) {
        strcat(cwd, DIR_SEPARATOR_STR);
    }

    while (--argc) {
        p = *++argv;

        if (*p == '-') {
            if (*++p == '\0') {
                usage_error();
            }

            while ((c = *p++)) {
                if (c == 'c') { fix_links = 1;
                } else if (c == 'd') { delete    = 1;
                } else if (c == 'o') { single_fs = 0;
                } else if (c == 'r') { recurse   = 1;
                } else if (c == 's') { shorten   = 1;
                } else if (c == 't') { testing   = 1;
				} else if (c == 'e') { CONTINUE_ON_ERROR = false;
                } else if (c == 'v') { verbose   = 1;
                } else {
                    usage_error();
                }
            }
        } else {
            struct stat st;

            if ( ISSLASH(*p)) {
                *path = '\0';
            } else {
                strcpy(path, cwd);
            }

            tidy_path(strcat(path, p));

            if (lstat(path, &st) == -1) {
				have_error(path);
            } else {
                dirwalk(path, strlen(path), st.st_dev);
            }

            ++dircount;
        }
    }

    if (dircount == 0) {
        usage_error();
    }

    exit(0);
}
