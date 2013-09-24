#!/bin/bash
export LD_PRELOAD=.libs/libnetresolve-getaddrinfo.so

exec libtool execute "$@"
