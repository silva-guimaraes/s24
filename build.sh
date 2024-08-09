#!/usr/bin/bash

set -e
xxd -n std -i std.s24 > std.c
gcc -lm s24.c -o s24
# -fsanitize=address -g

