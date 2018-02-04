#!/bin/bash
set -eu

docker pull "$2" || true
echo "Building $2"
docker build -f "$1" -t "$2" --cache-from "$2" .
