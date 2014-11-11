#!/bin/bash -e

DIFF="diff -u"
NR="./netresolve"
DATA="${srcdir:-.}/tests/data/dnssec"

export NETRESOLVE_CLAMP_TTL=1

for backend in aresdns:trust; do
    $DIFF <($NR --backends $backend --node www.nic.cz) $DATA/dns-forward
    $DIFF <($NR --backends $backend --address 195.47.235.3) $DATA/dns-reverse
    $DIFF <($NR --backends $backend --address 2a02:38::1001) $DATA/dns-reverse
done

for backend in ubdns aresdns aresdns:trust; do
    $DIFF <($NR --backends $backend --node www.rhybar.cz) $DATA/bogus
    $DIFF <($NR --backends $backend --node nonsense.rhybar.cz) $DATA/bogus
done
