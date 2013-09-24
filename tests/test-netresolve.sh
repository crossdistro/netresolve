#!/bin/bash -e

DIFF="diff -u"

$DIFF <(./netresolve) tests/data/any
$DIFF <(NETRESOLVE_FLAG_DEFAULT_LOOPBACK=yes ./netresolve) tests/data/localhost
$DIFF <(./netresolve localhost) tests/data/localhost
$DIFF <(./netresolve localhost4) tests/data/localhost4
$DIFF <(./netresolve localhost6) tests/data/localhost6
$DIFF <(./netresolve 1.2.3.4) tests/data/numeric4
$DIFF <(./netresolve 1.2.3.4%lo) tests/data/numeric4lo
$DIFF <(./netresolve 1.2.3.4%1) tests/data/numeric4lo
$DIFF <(./netresolve 1.2.3.4%999999) tests/data/numeric4nines
$DIFF <(./netresolve 1.2.3.4%999999x) tests/data/empty
$DIFF <(./netresolve 1:2:3:4:5:6:7:8) tests/data/numeric6
$DIFF <(./netresolve 1:2:3:4:5:6:7:8%lo) tests/data/numeric6lo
$DIFF <(./netresolve 1:2:3:4:5:6:7:8%999999) tests/data/numeric6nines
$DIFF <(./netresolve 1:2:3:4:5:6:7:8%999999x) tests/data/empty
$DIFF <(./netresolve a.root-servers.net) tests/data/dns
$DIFF <(./netresolve - exp1) tests/data/services
$DIFF <(./netresolve /path/to/socket) tests/data/unix
$DIFF <(./netresolve /path/to/socket - any stream) tests/data/unix-stream
$DIFF <(./netresolve /path/to/socket - any dgram) tests/data/unix-dgram
$DIFF <(./netresolve x-x-x-x-x-x-x-x-x) tests/data/empty
