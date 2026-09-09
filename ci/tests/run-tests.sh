#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

target=$1
if [ -z "${target-}" ]; then
    echo "Target not specified"
    exit 1
fi

echo "Running tests for target: ${target}"

# Enable core dumps for this process, unless the caller expects crashes
if [ -n "${NANO_DISABLE_CORE_DUMPS-}" ]; then
    ulimit -c 0
elif [ -n "${COREDUMP_DIR-}" ]; then
    ulimit -c unlimited
fi

# Run the test
shift
executable=$(get_test_executable "${target}")
"${executable}" "$@"
status=$?

if [ $status -ne 0 ]; then
    echo "::error::Test failed: ${target}"

    # Show core dumps if core dump collection is enabled
    if [ -n "${COREDUMP_DIR-}" ]; then
        "$(dirname "$BASH_SOURCE")/show-core-dumps.sh" "${executable}"
    fi

    exit $status
else
    exit 0
fi
