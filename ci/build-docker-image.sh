#!/bin/bash
set -eu

docker pull "$2" || true
echo "Building $2"
# Output for the build is WAY too long for Travis logs
docker build -f "$1" -t "$2" --cache-from "$2" . > /dev/null 2>&1
