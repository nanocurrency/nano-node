#!/bin/bash

set -e

REPO_ROOT=$(git rev-parse --show-toplevel)
cd "${REPO_ROOT}"
./ci/update-clang-format
find rai -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' | xargs clang-format -i
