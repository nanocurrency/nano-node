#!/usr/bin/env bash

pushd /tmp
wget -O qtbase-clang-latest.tgz https://s3.us-east-2.amazonaws.com/repo.nano.org/artifacts/qtbase-clang-latest.tgz
tar -zxf qtbase-clang-latest.tgz
mv tmp/* .
rm -fr tmp
popd
