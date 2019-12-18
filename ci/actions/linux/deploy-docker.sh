#!/bin/bash

set -e

if [ -n "$DOCKER_PASSWORD" ]; then
    echo "$DOCKER_PASSWORD" | docker login -u nanoreleaseteam --password-stdin

    scripts="$PWD/ci"
    TRAVIS_BRANCH=`git branch| cut -f2 -d' '`
    if [[ "$GITHUB_WORKFLOW" = "Develop" ]]; then
        "$scripts"/custom-timeout.sh 30 docker push "nanocurrency/nano-env:base"
        "$scripts"/custom-timeout.sh 30 docker push "nanocurrency/nano-env:gcc"
        "$scripts"/custom-timeout.sh 30 docker push "nanocurrency/nano-env:clang"
        echo "Deployed nano-env"
        exit 0
    else
        tags=()
        if [ -n "$TRAVIS_TAG" ]; then
            tags+=("$TRAVIS_TAG" latest)
            if [[ "$GITHUB_WORKFLOW" = "Beta" ]]; then
                tags+=(latest-including-rc)
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
        else
            echo "Nothing to deploy"
            exit 1
        fi
        docker_image_name="nanocurrency/nano${network_tag_suffix}"
        "$scripts"/custom-timeout.sh 30 docker build --build-arg NETWORK="$network" --build-arg CI_BUILD=true --build-arg TRAVIS_TAG="$TRAVIS_TAG" -f docker/node/Dockerfile -t "$docker_image_name" .
        for tag in "${tags[@]}"; do
            # Sanitize docker tag
            # https://docs.docker.com/engine/reference/commandline/tag/
            tag="$(printf '%s' "$tag" | tr -c '[a-z][A-Z][0-9]_.-' -)"
            if [ "$tag" != "latest" ]; then
                docker tag "$docker_image_name" "${docker_image_name}:$tag"
            fi
            "$scripts"/custom-timeout.sh 30 docker push "${docker_image_name}:$tag"
        done
    fi
    echo "$docker_image_name with tags ${tags[*]} deployed"
else
    echo "\$DOCKER_PASSWORD environment variable required"
    exit 1
fi