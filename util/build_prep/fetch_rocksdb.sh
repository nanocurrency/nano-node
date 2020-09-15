#!/usr/bin/env bash
OS=`uname`

pushd /tmp
wget -O rocksdb.tgz https://s3.us-east-2.amazonaws.com/repo.nano.org/artifacts/rocksdb-$OS-6.5.2.tgz

tar -zxf rocksdb.tgz
mv tmp/* .
rm -fr tmp
popd
