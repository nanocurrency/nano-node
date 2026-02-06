#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

target=$1
if [ -z "${target-}" ]; then
    echo "Target not specified"
    exit 1
fi

echo "Running tests for target: ${target}"

# Enable core dumps
setup_core_dumps

# Run the test
shift
executable=./${target}$(get_exec_extension)
"${executable}" "$@"
status=$?

if [ $status -ne 0 ]; then
    echo "::error::Test failed: ${target}"

    # Show core dumps
    export EXECUTABLE=${executable}
    "$(dirname "$BASH_SOURCE")/show-core-dumps.sh"

    exit $status
else
    exit 0
fi
