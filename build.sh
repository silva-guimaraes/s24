#!/usr/bin/bash

set -e
gcc -lm s24.c -o s24
# -fsanitize=address -g
