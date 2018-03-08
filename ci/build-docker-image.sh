#!/bin/bash
set -eu

if [ "$#" -lt 2 ]; then
	echo 'Usage: build-docker-image.sh <dockerFile> <dockerImageTag> [<dockerBuildArgs>...]' >&2
	exit 1
fi

dockerFile="$1"
dockerTag="$2"
shift; shift

scripts="$(dirname "$0")"

"$scripts"/custom-timeout.sh 20 docker pull "${dockerTag}" || true
echo "Building $dockerTag"
"$scripts"/custom-timeout.sh 30 docker build "$@" -f "${dockerFile}" -t "${dockerTag}" --cache-from "${dockerTag}" .
