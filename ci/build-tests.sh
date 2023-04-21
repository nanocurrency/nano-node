#!/bin/bash
set -euox pipefail

BUILD_TYPE="Debug"
if [[ "${RELEASE:-false}" == "true" ]]; then
    BUILD_TYPE="RelWithDebInfo"
fi

BUILD_TYPE=$BUILD_TYPE \
NANO_TEST=ON \
NANO_NETWORK=dev \
NANO_GUI=ON \
$(dirname "$BASH_SOURCE")/build.sh all_tests