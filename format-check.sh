#!/usr/bin/env bash

# Check code formatting for C++ and CMake files
# Usage: ./format-check.sh
#
# This script checks if your code is properly formatted.
# If it fails, run ./format-do.sh to fix the formatting.
#
# Required tools:
#   - clang-format version 17
#   - cmake-format version 0.6.13

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
    echo "Usage: ./format-check.sh"
    echo ""
    echo "Check code formatting for C++ and CMake files."
    echo "If this fails, run ./format-do.sh to fix the formatting."
    echo ""
    echo "Required tools:"
    echo "  - clang-format version 17"
    echo "  - cmake-format version 0.6.13"
    exit 0
fi

FAILED=0

echo "Checking C++ formatting..."
if ! "$SCRIPT_DIR/ci/clang-format-check.sh"; then
    echo "FAILED: C++ formatting (clang-format)"
    FAILED=1
fi

echo ""
echo "Checking CMake formatting..."
if ! "$SCRIPT_DIR/ci/cmake-format-check.sh"; then
    echo "FAILED: CMake formatting (cmake-format)"
    FAILED=1
fi

echo ""
if [[ $FAILED -eq 1 ]]; then
    echo "Formatting check failed. Run ./format-do.sh to fix."
    exit 1
else
    echo "All formatting checks passed."
fi
