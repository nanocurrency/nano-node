#!/bin/bash
set -euox pipefail

NANO_TEST=ON \
NANO_NETWORK=dev \
NANO_GUI=ON \
$(dirname "$BASH_SOURCE")/build.sh all_tests