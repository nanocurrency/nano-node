#!/bin/bash

set -e

scripts="$PWD/ci"
TRAVIS_BRANCH=$(git branch | cut -f2 -d' ')
tags=()
if [ -n "$TRAVIS_TAG" ]; then
    tags+=("$TRAVIS_TAG")
    if [[ "$GITHUB_WORKFLOW" = "Beta" || "$GITHUB_WORKFLOW" = "Test" ]]; then
        tags+=(latest latest-including-rc)
    fi
elif [ -n "$TRAVIS_BRANCH" ]; then
    TRAVIS_TAG=$TRAVIS_BRANCH
    tags+=("$TRAVIS_BRANCH")
fi
if [[ "$GITHUB_WORKFLOW" = "Live" ]]; then
    echo "Live"
    network_tag_suffix=''
    network="live"
elif [[ "$GITHUB_WORKFLOW" = "Beta" ]]; then
    echo "Beta"
    network_tag_suffix="-beta"
    network="beta"
elif [[ "$GITHUB_WORKFLOW" = "Test" ]]; then
    echo "Test"
    network_tag_suffix="-test"
    network="test"
fi
if [[ "$GITHUB_WORKFLOW" != "Develop" ]]; then
    docker_image_name="bananocoin/nano${network_tag_suffix}"
    ghcr_image_name="ghcr.io/${GITHUB_REPOSITORY}/nano${network_tag_suffix}"
    "$scripts"/build-docker-image.sh docker/node/Dockerfile "$docker_image_name" --build-arg NETWORK="$network" --build-arg CI_BUILD=true --build-arg TRAVIS_TAG="$TRAVIS_TAG"
    for tag in "${tags[@]}"; do
        # Sanitize docker tag
        # https://docs.docker.com/engine/reference/commandline/tag/
        tag="$(printf '%s' "$tag" | tr -c '[a-z][A-Z][0-9]_.-' -)"
        if [ "$tag" != "latest" ]; then
            docker tag "$docker_image_name" "${docker_image_name}:$tag"
            docker tag "$ghcr_image_name" "${ghcr_image_name}:$tag"
        fi
    done
fi

if [ -n "$DOCKER_PASSWORD" ]; then
    echo "$DOCKER_PASSWORD" | docker login -u nanoreleaseteam --password-stdin
    if [[ "$GITHUB_WORKFLOW" = "Develop" ]]; then
        "$scripts"/custom-timeout.sh 30 docker push "bananocoin/nano-env:base"
        "$scripts"/custom-timeout.sh 30 docker push "bananocoin/nano-env:gcc"
        "$scripts"/custom-timeout.sh 30 docker push "bananocoin/nano-env:clang-6"
        echo "Deployed nano-env"
        exit 0
    else
        tags=$(docker images --format '{{.Repository}}:{{.Tag }}' | grep bananocoin | grep -vE "env|ghcr.io|none")
        for a in $tags; do
            "$scripts"/custom-timeout.sh 30 docker push "$a"
        done
        echo "$docker_image_name with tags ${tags//$'\n'/' '} deployed"
    fi
else
    echo "\$DOCKER_PASSWORD environment variable required"
    exit 0
fi
