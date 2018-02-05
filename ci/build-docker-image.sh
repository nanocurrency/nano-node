#!/bin/bash
set -eu

scripts="$(dirname "$0")"

"$scripts"/custom-timeout.sh 20 docker pull "$2" || true
echo "Building $2"
"$scripts"/custom-timeout.sh 30 docker build -f "$1" -t "$2" --cache-from "$2" .
