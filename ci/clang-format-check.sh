#!/usr/bin/env bash

###################################################################################################

source "$(dirname "$BASH_SOURCE")/impl/common.sh"
source "$(dirname "$BASH_SOURCE")/impl/clang-format.sh"

###################################################################################################

does_clang_format_exist
if [[ $? == 0 ]]; then
    clang_format_check
    result=$?

    if [[ $result == 2 ]]; then
        exit $result
    fi

    if [[ $result == 1 ]]; then
        echo "Source code formatting differs from expected - please run ci/clang-format-do.sh"
        exit 1
    fi

    echo "clang-format check passed"
fi

###################################################################################################
