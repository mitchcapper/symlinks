#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([symlinks], [1.5.0], [brandt@runlevel1.com])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

gl_EARLY
gl_INIT


# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h malloc.h stddef.h stdlib.h string.h sys/param.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Get flags needed for large file support, if any
LFS_CFLAGS=$(getconf LFS_CFLAGS 2>/dev/null)
AM_CFLAGS="-Wall ${LFS_CFLAGS}"
AC_SUBST([AM_CFLAGS])

# Checks for library functions.
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_CHECK_FUNCS([getcwd strrchr strstr])

AC_CONFIG_FILES([Makefile symlinks.8])
AC_CONFIG_FILES([m4/Makefile])
AC_CONFIG_FILES([lib/Makefile])

AC_OUTPUT
