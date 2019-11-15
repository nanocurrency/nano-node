#!/usr/bin/env bash
OS=`uname`

if [[ $OS =~ "Darwin" ]]; then
	artifact="rocksdb-clang-latest.tgz"
else
	artifact="rocksdb-gcc-latest.tgz"
fi

pushd /tmp
wget -O $artifact https://s3.us-east-2.amazonaws.com/repo.nano.org/artifacts/$artifact
tar -zxf $artifact
mv tmp/* .
rm -fr tmp
popd
