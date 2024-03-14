#!/bin/bash
set -uo pipefail

echo "Analyzing core dumps..."

if [ -z "${COREDUMP_DIR-}" ]; then
    echo "COREDUMP_DIR environment variable is not set."
    exit 1
fi

if [ -z "${EXECUTABLE-}" ]; then
    echo "EXECUTABLE environment variable is not set."
    exit 1
fi

echo "Core dump location: ${COREDUMP_DIR}"
echo "Executable: ${EXECUTABLE}"

analyze_core_dump() {
    local core_dump=$1
    local executable=$2

    case "$(uname)" in
        Darwin)
            # macOS, use lldb
            echo "Using lldb for analysis..."
            lldb "${executable}" -c "$core_dump" --batch -o "thread backtrace all" -o "quit"
            ;;
        Linux)
            # Linux, use gdb
            echo "Using gdb for analysis..."
            gdb -quiet -batch -ex "thread apply all bt full" -ex "quit" "${executable}" "$core_dump"
            ;;
        *)
            echo "Unsupported OS."
            return 1
            ;;
    esac

    # Remove the analyzed core dump file
    echo "Removing analyzed core dump: $core_dump"
    rm "$core_dump"
}

# List core dump files
echo "::group::Core dump files"
ls -al "${COREDUMP_DIR}"
echo "::endgroup::"

# Use a glob pattern to match core dumps
shopt -s nullglob
core_dumps=("${COREDUMP_DIR}"/core*)

if [ ${#core_dumps[@]} -gt 0 ]; then
    for core_dump in "${core_dumps[@]}"; do
        echo "::group::Analyzing core dump: $core_dump"
        analyze_core_dump "$core_dump" "${EXECUTABLE}"
        echo "::endgroup::"
    done
else
    echo "No core dump file found."
fi
