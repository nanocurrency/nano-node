#!/usr/bin/env bash

FOUND=0

# hard requirement of clang-format 10.0.0
CF_EXECUTABLE='
  clang-format-10.0.0
  clang-format-10.0
  clang-format-10
  clang-format
'

# check different executable strings
for executable in $CF_EXECUTABLE; do
    if type -p "$executable" >/dev/null; then
        clang_format="$executable"
        FOUND=1
        break
    fi
done

# alert if not installed
if [ $FOUND == 0 ]; then
    echo "clang-format not found, please install 10.0.0 first"
    exit 1
fi

# check if old version is found
if ! "$clang_format" --version | grep '10.0.0' &>/dev/null; then
    echo "clang-format version 10.0.0 is required, please update it"
    exit 1
fi


REPO_ROOT=$(git rev-parse --show-toplevel)

"${REPO_ROOT}/ci/update-clang-format"

RESULT=$(python $REPO_ROOT/ci/git-clang-format.py --diff --extensions "hpp,cpp")
if [ "$RESULT" != "no modified files to format" ] && [ "$RESULT" != "clang-format did not modify any files" ]; then
    python $REPO_ROOT/ci/git-clang-format.py --diff --extensions "hpp,cpp"
    echo
    echo "Code formatting differs from expected - please run ci/clang-format-all.sh"
    exit 1
else
    echo "clang-format passed"
    exit 0
fi
