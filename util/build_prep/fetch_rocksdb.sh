#!/usr/bin/env bash
OS=`uname`
artifact="rocksdb-Linux-6.3.6-18.tgz"

pushd /tmp
wget -O $artifact https://s3.us-east-2.amazonaws.com/repo.nano.org/artifacts/$artifact
tar -zxf $artifact
mv tmp/* .
rm -fr tmp
popd
