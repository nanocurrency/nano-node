#!/usr/bin/env bash
OS=`uname`

TRAVIS_COMPILER="${TRAVIS_COMPILER:-clang}"

pushd /tmp
wget -O boost-$OS-$TRAVIS_COMPILER-latest.tgz https://s3.us-east-2.amazonaws.com/repo.nano.org/artifacts/boost-$OS-$TRAVIS_COMPILER-latest.tgz
tar -zxf boost-$OS-$TRAVIS_COMPILER-latest.tgz
mv tmp/* .
rm -fr tmp
popd
