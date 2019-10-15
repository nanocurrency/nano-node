#!/usr/bin/env bash

if [[ ! -d "/tmp/qt/lib/cmake" ]]; then
	popd /tmp
	wget -O qtbase-latest.tgz https://s3.us-east-2.amazonaws.com/repo.nano.org/artifacts/qtbase-latest.tgz
	tar -zxvf qtbase-latest.tgz
	mv tmp/* .
	rm -fr tmp
fi
