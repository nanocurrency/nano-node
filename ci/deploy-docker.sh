#!/bin/bash
set -eu

docker login -u nanocurrency -p "$DOCKER_PASSWORD"

# We push this just so it can be a cache next time
docker push nanocurrency/nano-ci

# We don't need to build this unless we're deploying it
ci/build-docker-image.sh docker/node/Dockerfile nanocurrency/nano
docker push nanocurrency/nano
