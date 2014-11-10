#!/bin/bash -e

DIFF="diff -u"
NR="./netresolve"
DATA="${srcdir:-.}/tests/data/dns"

export NETRESOLVE_CLAMP_TTL=1

for backend in {ub,ares}dns; do
    $DIFF <($NR --backends $backend --node www.nic.cz) $DATA/dns-forward
    $DIFF <($NR --backends $backend --srv --node xmpp.org --service xmpp-server) $DATA/dns-srv
    $DIFF <($NR --backends $backend --srv --node perseus.jabber.org --service xmpp-server) $DATA/dns-srv-fallback
    $DIFF <($NR --backends $backend --srv --node jabber.org --service stun --protocol udp) $DATA/dns-srv-udp
    $DIFF <($NR --backends $backend --address 195.47.235.3) $DATA/dns-reverse
    $DIFF <($NR --backends $backend --address 2a02:38::1001) $DATA/dns-reverse
done
