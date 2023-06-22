#!/bin/bash
set -u -x

if [ "$#" -lt 2 ]; then
	echo 'Usage: build-docker-image.sh <dockerFile> <dockerImageTag> [<dockerBuildArgs>...]' >&2
	exit 1
fi

dockerFile="$1"
dockerTag="$2"
githubTag="ghcr.io/${GITHUB_REPOSITORY}/${dockerTag#*/}"
shift
shift

echo "ci/build-docker-image.sh dockerFile=\"$dockerFile\" dockerTag=\"$dockerTag\" githubTag=\"$githubTag\""

scripts="$(dirname "$0")"

echo "Pulling $githubTag"
"$scripts"/custom-timeout.sh 20 docker pull "${githubTag}" || true

echo "Building $githubTag"
"$scripts"/custom-timeout.sh 60 docker build "$@" --build-arg REPOSITORY=${GITHUB_REPOSITORY} -f "${dockerFile}" -t "${githubTag}" --cache-from "${githubTag}" .

echo "Tagging ${dockerTag} from ${githubTag}"
docker tag $githubTag $dockerTag
