#!/usr/bin/env bash

set -e

source "$(dirname "$BASH_SOURCE")/detect-cmake-format.sh"
source "$(dirname "$BASH_SOURCE")/common.sh"

find "$REPO_ROOT" -iwholename "$REPO_ROOT/nano/*/CMakeLists.txt"   \
                  -o                                               \
                  -iwholename "$REPO_ROOT/CMakeLists.txt"          \
                  -o                                               \
                  -iwholename "$REPO_ROOT/coverage/CMakeLists.txt" \
     | xargs -I cmakeListsFile                                     \
             "$CMAKE_FORMAT" -i "cmakeListsFile"
