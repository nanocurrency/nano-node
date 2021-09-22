#!/usr/bin/env bash

set -e

source "$(dirname "$BASH_SOURCE")/detect-clang-format.sh"
source "$(dirname "$BASH_SOURCE")/common.sh"

find "$REPO_ROOT/nano" -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' | xargs -I source "$CLANG_FORMAT" -i -style=file "source"
