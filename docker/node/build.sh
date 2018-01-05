#!/bin/bash
REPO_ROOT=`git rev-parse --show-toplevel`
COMMIT_SHA=`git rev-parse --short HEAD`
pushd $REPO_ROOT
docker build -f docker/node/Dockerfile -t raiblocks-node:latest .
popd
