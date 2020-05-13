#!/bin/bash
REPO_ROOT=`git rev-parse --show-toplevel`
pushd $REPO_ROOT
docker build -f docker/build/Dockerfile.build -t bananocoin/banano-build:latest .
popd
