#!/usr/bin/env bash
OS=`uname`

COMPILER="${COMPILER:-clang}"

pushd /tmp
wget -O boost-$OS-$COMPILER.tgz https://s3.us-east-2.amazonaws.com/repo.nano.org/artifacts/boost-$OS-$COMPILER-1.70-full.tgz
tar -zxf boost-$OS-$COMPILER.tgz
mv tmp/* .
rm -fr tmp
popd
