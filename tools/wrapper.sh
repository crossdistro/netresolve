#!/bin/bash
export LD_PRELOAD="$(dirname $(dirname $0))/.libs/libnetresolve-libc.so"

exec libtool execute "$@"
