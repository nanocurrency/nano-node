#!/usr/bin/env bash

set -e

source "$(dirname "$BASH_SOURCE")/detect-clang-format.sh"
source "$(dirname "$BASH_SOURCE")/common.sh"

"$REPO_ROOT/ci/update-clang-format"

clang_format_result=$(python "$REPO_ROOT/ci/git-clang-format.py" --diff --extensions "hpp,cpp")
if [[ $clang_format_result != "no modified files to format" ]] &&
   [[ $clang_format_result != "clang-format did not modify any files" ]]; then
    python "$REPO_ROOT/ci/git-clang-format.py" --diff --extensions "hpp,cpp"
    echo
    echo "Code formatting differs from expected - please run ci/clang-format-all.sh"
    exit 1
else
    echo "clang-format passed"
    exit 0
fi
