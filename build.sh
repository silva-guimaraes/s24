#!/usr/bin/bash

set -e
gcc -lm main.c -o s24
# -fsanitize=address -g
