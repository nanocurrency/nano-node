#!/bin/bash

set -e

CLANG_FORMAT="clang-format"
if [ $(builtin type -p "$CLANG_FORMAT") ]; then
	REPO_ROOT=$(git rev-parse --show-toplevel)
	cd "$REPO_ROOT"
	./ci/update-clang-format
	find nano -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' | xargs "$CLANG_FORMAT" -i
else
	echo "'$CLANG_FORMAT' could not be detected in your PATH. Do you have it installed?"
fi
