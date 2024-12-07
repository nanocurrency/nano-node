#!/bin/bash
set -euo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

target=$1
if [ -z "${target-}" ]; then
    echo "Target not specified"
    exit 1
fi

executable=./${target}$(get_exec_extension)

# Check if gtest-parallel is available
if command -v gtest-parallel >/dev/null 2>&1; then
    echo "Running tests with gtest-parallel for target: ${target}"
    gtest-parallel "${executable}" --worker=1
else
    echo "gtest-parallel not found, running tests directly for target: ${target}"
    "${executable}"
fi 