#!/usr/bin/env bash

set -e

if [[ ! -z $(git status --untracked-files=no --porcelain) ]]; then
    echo "Unable to run script: working directory not clean (see git status)"
    exit 1
fi

source "$(dirname "$BASH_SOURCE")/common.sh"

"$REPO_ROOT/ci/clang-format-all.sh"

if [[ ! -z $(git status --untracked-files=no --porcelain) ]]; then
    echo "Code formatting differs from expected - please run ci/clang-format-all.sh"
    git diff
    git reset --hard HEAD > /dev/null
    exit 1
fi

echo "clang-format passed"
exit 0
