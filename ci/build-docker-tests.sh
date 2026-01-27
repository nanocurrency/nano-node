#!/bin/bash
set -euox pipefail

COMPILER=${COMPILER:-gcc}

echo "Building Docker test image with COMPILER=${COMPILER}"

docker build \
    --build-arg COMPILER=${COMPILER} \
    -f docker/tests/Dockerfile-tests \
    -t nano-test:latest \
    .
