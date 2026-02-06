#!/usr/bin/env bash

get_exec_extension() {
    case "$(uname -s)" in
        Linux*|Darwin*)     
            echo ""
            ;;
        CYGWIN*|MINGW32*|MSYS*|MINGW*)
            echo ".exe"
            ;;
        *)
            echo "Unknown OS"
            exit 1
            ;;
    esac
}

setup_core_dumps() {
    local DEFAULT_COREDUMP_DIR="/cores"
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
}