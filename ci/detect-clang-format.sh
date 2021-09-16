#!/bin/bash

set -e

CLANG_FORMAT="clang-format"
CLANG_FORMAT_VERSION="12"

if ! [ $(builtin type -p "$CLANG_FORMAT") ]; then
	echo "'$CLANG_FORMAT' could not be detected in your PATH. Do you have it installed?"
	exit 1
fi

VERSION_OUTPUT=$($CLANG_FORMAT --version)
if ! [[ $VERSION_OUTPUT =~ ^(.)*clang-format\ version\ $CLANG_FORMAT_VERSION(.)*$ ]]; then
	echo "Your '$CLANG_FORMAT' version is not '$CLANG_FORMAT_VERSION'. Please up/down grade it."
	exit 1
fi
