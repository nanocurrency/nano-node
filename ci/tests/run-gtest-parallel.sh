#!/bin/bash
set -euo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

target=$1
if [ -z "${target-}" ]; then
    echo "Target not specified"
    exit 1
fi

executable=./${target}$(get_exec_extension)

# Get the project root directory (2 levels up from ci/tests)
PROJECT_ROOT="$(cd "$(dirname "$BASH_SOURCE")/../.." && pwd)"
GTEST_PARALLEL="${PROJECT_ROOT}/submodules/gtest-parallel/gtest-parallel"

if [ -f "${GTEST_PARALLEL}" ]; then
    echo "Running tests with gtest-parallel for target: ${target}"
    "${GTEST_PARALLEL}" "${executable}" --worker=1
else
    echo "gtest-parallel not found at ${GTEST_PARALLEL}, running tests directly for target: ${target}"
    "${executable}"
fi 