#!/bin/bash
set -e

./bootstrap --skip-po --force
./configure
make
