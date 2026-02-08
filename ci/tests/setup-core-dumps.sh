#!/bin/bash

# System-level core dump setup for CI environments

DEFAULT_COREDUMP_DIR="/cores"

case "$(uname -s)" in
    Linux*)
        # Ensure directory exists and is writable for core dumps
        sudo mkdir -p "${DEFAULT_COREDUMP_DIR}"
        sudo chmod a+w "${DEFAULT_COREDUMP_DIR}"
        # Remove any leftover core dumps from previous runs
        rm -f "${DEFAULT_COREDUMP_DIR}"/core*
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
        # Remove any leftover core dumps from previous runs
        rm -f "${DEFAULT_COREDUMP_DIR}"/core*
        # Enable core dumps
        ulimit -c unlimited
        # Configure core dump filename to include executable name (%N) and PID (%P)
        sudo sysctl kern.corefile="${DEFAULT_COREDUMP_DIR}/core-%N.%P"

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

# Persist COREDUMP_DIR for subsequent GitHub Actions steps
if [ -n "${GITHUB_ENV-}" ]; then
    echo "COREDUMP_DIR=${COREDUMP_DIR-}" >> "${GITHUB_ENV}"
fi
