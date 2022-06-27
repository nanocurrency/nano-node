#!/bin/bash

set -e
set -a

scripts="$PWD/ci"
CI_BRANCH=$(git branch | cut -f2 -d' ')
tags=()
if [ -n "$CI_TAG" ]; then
    tags+=("$CI_TAG")
elif [ -n "$CI_BRANCH" ]; then
    CI_TAG=$CI_BRANCH
    tags+=("$CI_BRANCH")
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
    docker_image_name="nanocurrency/nano${network_tag_suffix}"
fi

docker_build()
{
    if [[ "$GITHUB_WORKFLOW" != "Develop" ]]; then
        ghcr_image_name="ghcr.io/${GITHUB_REPOSITORY}/nano${network_tag_suffix}"
        "$scripts"/build-docker-image.sh docker/node/Dockerfile "$docker_image_name" --build-arg NETWORK="$network" --build-arg CI_BUILD=true --build-arg CI_TAG="$CI_TAG"
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
}

docker_deploy()
{
    if [ -n "$DOCKER_PASSWORD" ]; then
        echo "$DOCKER_PASSWORD" | docker login -u nanoreleaseteam --password-stdin
        if [[ "$GITHUB_WORKFLOW" = "Develop" ]]; then
            "$scripts"/custom-timeout.sh 30 docker push "nanocurrency/nano-env:base"
            "$scripts"/custom-timeout.sh 30 docker push "nanocurrency/nano-env:gcc"
            "$scripts"/custom-timeout.sh 30 docker push "nanocurrency/nano-env:clang-6"
            echo "Deployed nano-env"
            exit 0
        else
            if [[ "$GITHUB_WORKFLOW" = "Live" ]]; then
                tags=$(docker images --format '{{.Repository}}:{{.Tag }}' | grep nanocurrency | grep -vE "env|ghcr.io|none|latest")
            else
                tags=$(docker images --format '{{.Repository}}:{{.Tag }}' | grep nanocurrency | grep -vE "env|ghcr.io|none")
            fi
            for a in $tags; do
                "$scripts"/custom-timeout.sh 30 docker push "$a"
            done
            echo "$docker_image_name with tags ${tags//$'\n'/' '} deployed"
        fi
    else
        echo "\$DOCKER_PASSWORD environment variable required"
        exit 0
    fi
}
