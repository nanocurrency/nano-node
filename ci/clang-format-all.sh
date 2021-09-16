#!/bin/bash

set -e

source detect-clang-format.sh
source common.sh

cd "$REPO_ROOT"
./ci/update-clang-format
find nano -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' | xargs "$CLANG_FORMAT" -i
