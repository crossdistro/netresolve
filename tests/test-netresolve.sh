#!/bin/bash -e

DIFF="diff -u"
NR="./netresolve"
DATA="${srcdir:-.}/tests/data"

# empty
$DIFF <($NR) $DATA/any
$DIFF <($NR --backends=any) $DATA/any
$DIFF <($NR --backends=numerichost) $DATA/empty
$DIFF <($NR --backends=loopback) $DATA/localhost
$DIFF <($NR --backends=hosts) $DATA/empty
$DIFF <($NR --backends=ubdns) $DATA/empty
$DIFF <($NR --backends=aresdns) $DATA/empty
$DIFF <($NR --backends=getaddrinfo) $DATA/empty
$DIFF <($NR --backends=nss:files) $DATA/empty
$DIFF <($NR --backends=nss:bogusbogus) $DATA/empty
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so) $DATA/any
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:getaddrinfo) $DATA/any
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:gethostbyname4) $DATA/empty
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:gethostbyname3) $DATA/empty
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:gethostbyname2) $DATA/empty
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:gethostbyname) $DATA/empty

# empty (passive)
$DIFF <(NETRESOLVE_FLAG_DEFAULT_LOOPBACK=yes $NR) $DATA/localhost

# empty/http
$DIFF <($NR --service http) $DATA/services
$DIFF <($NR --backend=getaddrinfo --service http) $DATA/services

# numeric
$DIFF <($NR --node 1.2.3.4) $DATA/numeric4
$DIFF <($NR --node 1.2.3.4%lo) $DATA/numeric4lo
$DIFF <($NR --node 1.2.3.4%1) $DATA/numeric4lo
$DIFF <($NR --node 1.2.3.4%999999) $DATA/numeric4nines
$DIFF <($NR --node 1.2.3.4%999999x) $DATA/empty
$DIFF <($NR --node 1:2:3:4:5:6:7:8) $DATA/numeric6
$DIFF <($NR --node 1:2:3:4:5:6:7:8%lo) $DATA/numeric6lo
$DIFF <($NR --node 1:2:3:4:5:6:7:8%999999) $DATA/numeric6nines
$DIFF <($NR --node 1:2:3:4:5:6:7:8%999999x) $DATA/empty

# localhost
$DIFF <($NR --node localhost) $DATA/localhost
$DIFF <($NR --backends=hosts --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:files --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:files:gethostbyname4 --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:files:gethostbyname3 --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:files:gethostbyname2 --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:files:gethostbyname --node localhost) $DATA/localhost4
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:getaddrinfo --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:gethostbyname4 --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:gethostbyname3 --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:gethostbyname2 --node localhost) $DATA/localhost
$DIFF <($NR --backends=nss:./.libs/libnss_netresolve.so:gethostbyname --node localhost) $DATA/localhost4

# localhost/http
$DIFF <($NR --node localhost) $DATA/localhost
$DIFF <($NR --backends=getaddrinfo --node localhost --service http) $DATA/localhost-http
$DIFF <($NR --backends=nss:files --node localhost --service http) $DATA/localhost-http
$DIFF <($NR --backends=nss:files:gethostbyname4 --node localhost --service http) $DATA/localhost-http
$DIFF <($NR --backends=nss:files:gethostbyname3 --node localhost --service http) $DATA/localhost-http
$DIFF <($NR --backends=nss:files:gethostbyname2 --node localhost --service http) $DATA/localhost-http
$DIFF <($NR --backends=nss:files:gethostbyname --node localhost --service http) $DATA/localhost4-http

# localhost (ip4)
$DIFF <($NR --node localhost --family ip4) $DATA/localhost4
$DIFF <($NR --backends=nss:files --node localhost --family ip4) $DATA/localhost4
$DIFF <($NR --backends=nss:files:gethostbyname4 --node localhost --family ip4) $DATA/empty
$DIFF <($NR --backends=nss:files:gethostbyname3 --node localhost --family ip4) $DATA/localhost4
$DIFF <($NR --backends=nss:files:gethostbyname2 --node localhost --family ip4) $DATA/localhost4
$DIFF <($NR --backends=nss:files:gethostbyname --node localhost --family ip4) $DATA/localhost4

# localhost (ip6)
$DIFF <($NR --node localhost --family ip6) $DATA/localhost6
$DIFF <($NR --backends=nss:files --node localhost --family ip6) $DATA/localhost6
$DIFF <($NR --backends=nss:files:gethostbyname4 --node localhost --family ip6) $DATA/empty
$DIFF <($NR --backends=nss:files:gethostbyname3 --node localhost --family ip6) $DATA/localhost6
$DIFF <($NR --backends=nss:files:gethostbyname2 --node localhost --family ip6) $DATA/localhost6
$DIFF <($NR --backends=nss:files:gethostbyname --node localhost --family ip6) $DATA/empty

# localhost4
$DIFF <($NR --node localhost4) $DATA/localhost4

# localhost6
$DIFF <($NR --node localhost6) $DATA/localhost6

# bogus
$DIFF <($NR --node x-x-x-x-x-x-x-x-x) $DATA/empty

# unix
$DIFF <($NR --node /path/to/socket --family unix) $DATA/unix
$DIFF <($NR --node /path/to/socket --family unix --socktype stream) $DATA/unix-stream
$DIFF <($NR --node /path/to/socket --family unix --socktype dgram) $DATA/unix-dgram
