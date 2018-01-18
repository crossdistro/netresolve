#!/bin/sh

git archive HEAD \
    --prefix netresolve-0.0.1/ -o netresolve-0.0.1.tar.xz
rpmbuild \
    -D "_sourcedir $PWD" -D "_rpmdir $PWD" -D "_rpmdir $PWD" \
    -ba contrib/rpm/netresolve.spec
