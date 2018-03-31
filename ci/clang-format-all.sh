#!/bin/bash

REPO_ROOT=$(git rev-parse --show-toplevel)
find ${REPO_ROOT}/rai ${REPO_ROOT}/rocksdb-lmdb-wrapper -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' | xargs clang-format -i
