#!/usr/bin/env bash

set -e

is_cmake_format_usable()
{
    if [[ $(builtin type -p $1) ]]; then
        local output=$($1 --version)
        if [[ $output =~ ^(.)*$2(.)*$ ]]; then
            echo "0"
        else
            echo $output
        fi
    else
        echo "1"
    fi
}

CMAKE_FORMAT=""
CMAKE_FORMAT_VERSION="0.6.13"

cmake_format_attempts=("cmake-format")

for itr in ${cmake_format_attempts[@]}; do
    result=$(is_cmake_format_usable $itr $CMAKE_FORMAT_VERSION)
    if [[ $result == "0" ]]; then
        CMAKE_FORMAT=$itr
        break
    elif [[ $result == "1" ]]; then
        continue
    else
        echo "Detected '$itr' with version '$result' " \
             "(different than '$CMAKE_FORMAT_VERSION'), skipping it."
    fi
done

if [[ -z $CMAKE_FORMAT ]]; then
    echo "No 'cmake-format' of version '$CMAKE_FORMAT_VERSION' could be detected in your PATH." \
         "Try pip/pip3 install cmake-format. Or try up/down-grading if you installed it differently."
    exit 1
fi

echo "Using '$CMAKE_FORMAT' version '$CMAKE_FORMAT_VERSION'"
