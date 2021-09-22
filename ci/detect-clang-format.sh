#!/usr/bin/env bash

set -e

is_clang_format_usable()
{
    if [[ $(builtin type -p $1) ]]; then
        local output=$($1 --version)
        if [[ $output =~ ^(.)*clang-format\ version\ $2(.)*$ ]]; then
            echo "0"
        else
            echo $output
        fi
    else
        echo "1"
    fi
}

CLANG_FORMAT=""
CLANG_FORMAT_VERSION="12"

clang_format_attempts=("clang-format"
                       "clang-format-$CLANG_FORMAT_VERSION")

for itr in ${clang_format_attempts[@]}; do
    result=$(is_clang_format_usable $itr $CLANG_FORMAT_VERSION)
    if [[ $result == "0" ]]; then
        CLANG_FORMAT=$itr
        break
    elif [[ $result == "1" ]]; then
        continue
    else
        echo "Detected '$itr' with version '$result' " \
             "(different than '$CLANG_FORMAT_VERSION'), skipping it."
    fi
done

if [[ -z $CLANG_FORMAT ]]; then
    echo "No 'clang-format' of version '$CLANG_FORMAT_VERSION' could be detected in your PATH."
    exit 1
fi

echo "Using '$CLANG_FORMAT' version '$CLANG_FORMAT_VERSION'"
