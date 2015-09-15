#!/bin/bash -xe

mkdir -p m4
autoreconf --install --symlink

if [ -z "$NOCONFIGURE" ]; then
    ./configure "$@";
fi
