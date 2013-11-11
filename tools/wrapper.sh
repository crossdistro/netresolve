#!/bin/bash
export LD_PRELOAD=.libs/libnetresolve-posix.so

exec libtool execute "$@"
