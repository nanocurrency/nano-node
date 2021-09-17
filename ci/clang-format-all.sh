#!/usr/bin/env bash

set -e

source $(dirname $BASH_SOURCE)/detect-clang-format.sh
source $(dirname $BASH_SOURCE)/common.sh

cd "$REPO_ROOT"
./ci/update-clang-format
find nano -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' | xargs "$CLANG_FORMAT" -i
