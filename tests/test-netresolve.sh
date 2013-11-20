#!/bin/bash -e

DIFF="diff -u"
NR="./netresolve"
DATA="${srcdir:-.}/tests/data"

$DIFF <($NR) $DATA/any
$DIFF <(NETRESOLVE_FLAG_DEFAULT_LOOPBACK=yes $NR) $DATA/localhost
$DIFF <($NR localhost) $DATA/localhost
$DIFF <($NR localhost4) $DATA/localhost4
$DIFF <($NR localhost6) $DATA/localhost6
$DIFF <($NR 1.2.3.4) $DATA/numeric4
$DIFF <($NR 1.2.3.4%lo) $DATA/numeric4lo
$DIFF <($NR 1.2.3.4%1) $DATA/numeric4lo
$DIFF <($NR 1.2.3.4%999999) $DATA/numeric4nines
$DIFF <($NR 1.2.3.4%999999x) $DATA/empty
$DIFF <($NR 1:2:3:4:5:6:7:8) $DATA/numeric6
$DIFF <($NR 1:2:3:4:5:6:7:8%lo) $DATA/numeric6lo
$DIFF <($NR 1:2:3:4:5:6:7:8%999999) $DATA/numeric6nines
$DIFF <($NR 1:2:3:4:5:6:7:8%999999x) $DATA/empty
#$DIFF <($NR a.root-servers.net) $DATA/dns
$DIFF <($NR - exp1) $DATA/services
$DIFF <($NR /path/to/socket - unix) $DATA/unix
$DIFF <($NR /path/to/socket - unix stream) $DATA/unix-stream
$DIFF <($NR /path/to/socket - unix dgram) $DATA/unix-dgram
$DIFF <($NR x-x-x-x-x-x-x-x-x) $DATA/empty
