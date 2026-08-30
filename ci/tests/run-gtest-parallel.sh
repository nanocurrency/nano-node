#!/bin/bash
set -euo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

executable=$1
if [ -z "${executable-}" ]; then
    echo "Executable not specified"
    exit 1
fi

shift

# Get the project root directory (2 levels up from ci/tests)
PROJECT_ROOT="$(cd "$(dirname "$BASH_SOURCE")/../.." && pwd)"
GTEST_PARALLEL="${PROJECT_ROOT}/submodules/gtest-parallel/gtest-parallel"

if [ -f "${GTEST_PARALLEL}" ]; then
    echo "Running tests with gtest-parallel for executable: ${executable}"
    "${GTEST_PARALLEL}" "${executable}" --worker=1 "$@"
    test_status=$?
else
    echo "gtest-parallel not found at ${GTEST_PARALLEL}, running tests directly for executable: ${executable}"
    "${executable}" "$@"
    test_status=$?
fi

# Return the original test exit status
exit $test_status