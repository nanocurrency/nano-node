#!/bin/bash

set -o errexit
set -o nounset
set -o xtrace
OS=$(uname)

if [[ "${BETA-0}" -eq 1 ]]; then
    BUILD="beta"
else
    BUILD="live"
fi

if [[ "$OS" == 'Linux' ]]; then
    aws s3 cp $TRAVIS_BUILD_DIR/build/nano-node-*-Linux.tar.bz2 s3://$AWS_BUCKET/$BUILD/binaries/nano-node-$TRAVIS_TAG-Linux.tar.bz2 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
else
    aws s3 cp $TRAVIS_BUILD_DIR/build/nano-node-*-Darwin.dmg s3://$AWS_BUCKET/$BUILD/binaries/nano-node-$TRAVIS_TAG-Darwin.dmg --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
fi
