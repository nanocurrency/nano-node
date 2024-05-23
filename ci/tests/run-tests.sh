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
DEFAULT_COREDUMP_DIR="/cores"
case "$(uname -s)" in
    Linux*)
        # Ensure directory exists and is writable for core dumps
        sudo mkdir -p "${DEFAULT_COREDUMP_DIR}"
        sudo chmod a+w "${DEFAULT_COREDUMP_DIR}"
        # Enable core dumps
        ulimit -c unlimited
        echo "${DEFAULT_COREDUMP_DIR}/core-%e.%p" | sudo tee /proc/sys/kernel/core_pattern
        export COREDUMP_DIR=${DEFAULT_COREDUMP_DIR}

        echo "Core dumps enabled (Linux)"
        ;;
    Darwin*)
        # Ensure directory exists and is writable for core dumps
        sudo mkdir -p "${DEFAULT_COREDUMP_DIR}"
        sudo chmod a+w "${DEFAULT_COREDUMP_DIR}"
        # Enable core dumps
        ulimit -c unlimited
        # By default, macOS writes core dumps to /cores
        export COREDUMP_DIR=${DEFAULT_COREDUMP_DIR}

        echo "Core dumps enabled (macOS)"
        ;;
    CYGWIN*|MINGW32*|MSYS*|MINGW*)
        # TODO: Support core dumps on Windows
        echo "Core dumps not supported on Windows"
        ;;
    *)
        echo "Unknown OS"
        exit 1
        ;;
esac

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
