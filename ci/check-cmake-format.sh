#!/usr/bin/env bash

set -e

if [[ ! -z $(git status --untracked-files=no --porcelain) ]]; then
    echo "Unable to run script: working directory not clean (see git status)"
    exit 1
fi

source "$(dirname "$BASH_SOURCE")/common.sh"

"$REPO_ROOT/ci/cmake-format-all.sh"

if [[ ! -z $(git status --untracked-files=no --porcelain) ]]; then
    echo "CMake formatting differs from expected - please run ci/cmake-format-all.sh"
    git diff
    git reset --hard HEAD > /dev/null
    exit 1
fi

echo "cmake-format passed"
exit 0
