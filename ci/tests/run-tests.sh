#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

target=$1
if [ -z "${target-}" ]; then
    echo "Target not specified"
    exit 1
fi

echo "Running tests for target: ${target}"

# Enable core dumps for this process
if [ -n "${COREDUMP_DIR-}" ]; then
    ulimit -c unlimited
fi

shift
executable=./${target}$(get_exec_extension)

# Run the test using gtest-parallel helper
"$(dirname "$BASH_SOURCE")/run-gtest-parallel.sh" "${executable}" "$@"
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
