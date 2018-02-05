#!/bin/bash
set -eu

scripts="$(dirname "$0")"

docker login -u nanocurrency -p "$DOCKER_PASSWORD"

# We push this just so it can be a cache next time
"$scripts"/custom-timeout.sh 30 docker push nanocurrency/nano-ci

# We don't need to build this unless we're deploying it
ci/build-docker-image.sh docker/node/Dockerfile nanocurrency/nano
"$scripts"/custom-timeout.sh 30 docker push nanocurrency/nano
