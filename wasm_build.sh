#!/usr/bin/bash

./build.sh

xxd -n std -i std.s24 > std.c
emcc s24.c -o web/wasm.js -sEXPORTED_FUNCTIONS=_main,_run_from_string -sEXPORTED_RUNTIME_METHODS=cwrap
