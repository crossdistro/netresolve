#!/bin/bash -e

for name in getaddrinfo gethostbyname{,2,_r,2_r}; do
    ./test-$name
    ./wrapresolve ./test-$name
done
