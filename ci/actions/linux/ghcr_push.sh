#!/bin/bash
set -e

scripts="$PWD/ci"
LOWER_GITHUB_REPOSITORY = echo "${GITHUB_REPOSITORY}" | tr '[:lower:]'
if [[ "$GITHUB_WORKFLOW" = "Develop" ]]; then
    "$scripts"/custom-timeout.sh 30 docker push "ghcr.io/${LOWER_GITHUB_REPOSITORY}/nano-env:base"
    "$scripts"/custom-timeout.sh 30 docker push "ghcr.io/${LOWER_GITHUB_REPOSITORY}/nano-env:gcc"
    "$scripts"/custom-timeout.sh 30 docker push "ghcr.io/${LOWER_GITHUB_REPOSITORY}/nano-env:clang-6"
else
    tags=$(docker images --format '{{.Repository}}:{{.Tag }}' | grep "ghcr.io" | grep -vE "env|none")
    for a in $tags; do
        "$scripts"/custom-timeout.sh 30 docker push "$a"
    done
fi
