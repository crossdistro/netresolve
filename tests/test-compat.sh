#!/bin/bash -e

DIFF="diff -u"
DATA="${srcdir:-.}/tests/data"

test_command() {
    $DIFF <(./$@) tests/data/$1
    $DIFF <(./wrapresolve ./"$@") tests/data/$1
}

for name in getaddrinfo gethostbyname{,2,_r,2_r}; do
    ./test-$name
    ./wrapresolve ./test-$name
done

./wrapresolve ./test-asyncns

test_command getnameinfo --address 127.0.0.1 --port 80
test_command gethostbyaddr --address 127.0.0.1
