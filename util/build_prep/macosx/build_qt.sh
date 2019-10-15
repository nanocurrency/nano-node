#!/usr/bin/env bash

if [[ ! -d "/tmp/qt/lib/cmake" ]]; then
	aws s3 cp s3://$AWS_BUCKET/artifacts/qtbase-latest.tgz /tmp/qtbase-latest.tgz
	popd /tmp
	tar -zxvf qtbase-latest.tgz
	mv tmp/* .
	rm -fr tmp
fi
