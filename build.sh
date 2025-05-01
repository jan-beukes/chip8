#!/bin/sh

set -xe

CFLAGS='-Wall -Wextra'

cc $CFLAGS -o emu *.c -lraylib -lm
