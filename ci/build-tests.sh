#!/bin/bash
set -euox pipefail

CELERIX_TEST=ON \
CELERIX_NETWORK=dev \
CELERIX_GUI=ON \
$(dirname "$BASH_SOURCE")/build.sh all_tests