#!/bin/bash -e

DIFF="diff -u"
NR="./netresolve"
DATA="${srcdir:-.}/tests/data"

$DIFF <($NR) $DATA/any
$DIFF <(NETRESOLVE_FLAG_DEFAULT_LOOPBACK=yes $NR) $DATA/localhost
$DIFF <($NR --node localhost) $DATA/localhost
$DIFF <($NR --node localhost4) $DATA/localhost4
$DIFF <($NR --node localhost6) $DATA/localhost6
$DIFF <($NR --node 1.2.3.4) $DATA/numeric4
$DIFF <($NR --node 1.2.3.4%lo) $DATA/numeric4lo
$DIFF <($NR --node 1.2.3.4%1) $DATA/numeric4lo
$DIFF <($NR --node 1.2.3.4%999999) $DATA/numeric4nines
$DIFF <($NR --node 1.2.3.4%999999x) $DATA/empty
$DIFF <($NR --node 1:2:3:4:5:6:7:8) $DATA/numeric6
$DIFF <($NR --node 1:2:3:4:5:6:7:8%lo) $DATA/numeric6lo
$DIFF <($NR --node 1:2:3:4:5:6:7:8%999999) $DATA/numeric6nines
$DIFF <($NR --node 1:2:3:4:5:6:7:8%999999x) $DATA/empty
#$DIFF <($NR a.root-servers.net) $DATA/dns
$DIFF <($NR --service exp1) $DATA/services
$DIFF <($NR --node /path/to/socket --family unix) $DATA/unix
$DIFF <($NR --node /path/to/socket --family unix --socktype stream) $DATA/unix-stream
$DIFF <($NR --node /path/to/socket --family unix --socktype dgram) $DATA/unix-dgram
$DIFF <($NR --node x-x-x-x-x-x-x-x-x) $DATA/empty
