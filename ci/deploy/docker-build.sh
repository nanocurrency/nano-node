#!/bin/bash
set -euo pipefail

# Builds the node image for one network and hands its local name and registry name to the push steps.
# NETWORK picks beta or live, CI_TAG is the build tag baked into the binaries.
# A live build of anything but a release tag is a nightly, published under its own name.

die() {
    echo "::error::$*" >&2
    exit 1
}

tag=${CI_TAG:-}
if [[ -z "${tag}" ]]; then
    die "CI_TAG must name the build tag"
fi

case "${NETWORK:-}" in
    beta)
        network=beta
        name=nano-beta
        ;;
    live)
        network=live
        if [[ "${tag}" =~ ^V[0-9]+\.[0-9]+$ ]]; then
            name=nano
        else
            name=nano-nightly
        fi
        ;;
    *)
        die "NETWORK must be beta or live, got '${NETWORK:-}'"
        ;;
esac

image="nano-node:${tag}"
echo "Building the ${network} network image for ${tag} as ${image}, to be published as ${name}"

docker build \
    --file docker/node/Dockerfile \
    --tag "${image}" \
    --build-arg NANO_NETWORK="${network}" \
    --build-arg CI_TAG="${tag}" \
    --build-arg REPOSITORY="${GITHUB_REPOSITORY:-nanocurrency/nano-node}" \
    .

echo "Built ${image}"
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    {
        echo "image=${image}"
        echo "name=${name}"
    } >>"${GITHUB_OUTPUT}"
fi
