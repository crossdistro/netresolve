#!/bin/sh -e
mkdir -p m4
autoreconf --install --symlink
./configure "$@"
