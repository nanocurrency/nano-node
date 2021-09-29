#!/usr/bin/env bash

###################################################################################################

source "$(dirname "$BASH_SOURCE")/impl/common.sh"
source "$(dirname "$BASH_SOURCE")/impl/cmake-format.sh"

###################################################################################################

does_cmake_format_exist
if [[ $? == 0 ]]; then
    cmake_format_check
    result=$?

    if [[ $result == 2 ]]; then
        exit $result
    fi

    if [[ $result == 1 ]]; then
        echo "CMake formatting differs from expected - please run ci/cmake-format-do.sh"
        exit 1
    fi

    echo "cmake-format check passed"
fi

###################################################################################################
