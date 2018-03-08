#!/bin/bash
set -eu

scripts="$(dirname "$0")"

docker login -u nanocurrency -p "$DOCKER_PASSWORD"

# We push this just so it can be a cache next time
"$scripts"/custom-timeout.sh 30 docker push nanocurrency/nano-ci

tags=()
if [ -n "$TRAVIS_BRANCH" ]; then
    tags+=("$TRAVIS_BRANCH")
elif [ -n "$TRAVIS_TAG" ]; then
    tags+=("$TRAVIS_TAG" latest)
fi

for network in live beta; do
    if [ "${network}" = 'live' ]; then
        network_tag_suffix=''
    else
        network_tag_suffix="-${network}"
    fi

    docker_image_name="nanocurrency/nano${network_tag_suffix}"

    ci/build-docker-image.sh docker/node/Dockerfile "$docker_image_name" --build-arg NETWORK="${network}"
    for tag in "${tags[@]}"; do
        # Sanitize docker tag
        # https://docs.docker.com/engine/reference/commandline/tag/
        tag="$(printf '%s' "$tag" | tr -c '[a-z][A-Z][0-9]_.-' -)"
        if [ "$tag" != "latest" ]; then
            docker tag "$docker_image_name" "${docker_image_name}:$tag"
        fi
        "$scripts"/custom-timeout.sh 30 docker push "${docker_image_name}:$tag"
    done
done
