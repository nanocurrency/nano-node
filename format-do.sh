#!/usr/bin/env bash

# Format C++ and CMake files
# Usage: ./format-do.sh
#
# This script formats all C++ and CMake files in the project.
# Run ./format-check.sh to verify formatting without making changes.
#
# Required tools:
#   - clang-format version 17
#   - cmake-format version 0.6.13

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
    echo "Usage: ./format-do.sh"
    echo ""
    echo "Format all C++ and CMake files in the project."
    echo "Run ./format-check.sh to verify formatting without making changes."
    echo ""
    echo "Required tools:"
    echo "  - clang-format version 17"
    echo "  - cmake-format version 0.6.13"
    exit 0
fi

echo "Formatting C++ files..."
"$SCRIPT_DIR/ci/clang-format-do.sh"

echo ""
echo "Formatting CMake files..."
"$SCRIPT_DIR/ci/cmake-format-do.sh"

echo ""
echo "Formatting complete."
