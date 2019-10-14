#!/usr/bin/env bash

if [[ ! -d "/tmp/qt/lib/cmake" ]]; then
	pushd /tmp
	wget -O qtbase-latest.tgz https://s3.us-east-2.amazonaws.com/$AWS_BUCKET/artifacts/qtbase-latest.tgz
	tar -zxf qtbase-latest.tgz
	mv tmp/* .
	rm -fr tmp
	popd
fi
