#!/usr/bin/env bash

set -e

source "$(dirname "$BASH_SOURCE")/common.sh"

if ! [[ $(builtin type -p cmake-format) ]]; then
    echo "pip install cmake-format to continue"
    exit 1
fi

cd "$REPO_ROOT"

find "$REPO_ROOT" -iwholename "$REPO_ROOT/nano/*/CMakeLists.txt"   \
                  -o                                               \
                  -iwholename "$REPO_ROOT/CMakeLists.txt"          \
                  -o                                               \
                  -iwholename "$REPO_ROOT/coverage/CMakeLists.txt" \
     | xargs -i{} cmake-format -i {}
