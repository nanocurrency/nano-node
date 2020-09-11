#!/usr/bin/env bash
OS=`uname`


pushd /tmp
if [[ "$OS" == 'Linux' ]]; then
    wget -O rocksdb.tgz https://s3.us-east-2.amazonaws.com/repo.nano.org/artifacts/rocksdb-Linux-6.3.6-18.tgz
else
    wget -O rocksdb.tgz https://s3.us-east-2.amazonaws.com/repo.nano.org/artifacts/rocksdb-clang-latest.tgz
fi 

tar -zxf rocksdb.tgz
mv tmp/* .
rm -fr tmp
popd
