#!/usr/bin/env bash
REPO_ROOT=$(git rev-parse --show-toplevel)
RESULT=`python $REPO_ROOT/ci/git-clang-format.py --diff -f --commit HEAD~1 --extensions "h,hpp,cpp"`
if [ "$RESULT" != "no modified files to format" ] && [ "$RESULT" != "clang-format did not modify any files" ]; then
    python $REPO_ROOT/ci/git-clang-format.py --diff -f --commit HEAD~1
    echo
    echo "Code formatting differs from expected - please run ci/clang-format-all.sh"
    exit 1
fi
