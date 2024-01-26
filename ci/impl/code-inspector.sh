#!/usr/bin/env bash

###################################################################################################

code_inspect()
{
    local SOURCE_ROOT_PATH=$1
    if [[ $SOURCE_ROOT_PATH == "" ]]; then
        echo "Missing the source code path" >&2
        return 1
    fi

    # This is to prevent out of scope access in async_write from asio which is not picked up by static analysers
    if [[ $(grep -rl --exclude="*asio.hpp" "asio::async_write" $SOURCE_ROOT_PATH/nano) ]]; then
        echo "Using boost::asio::async_write directly is not permitted (except in nano/lib/asio.hpp). Use nano::async_write instead" >&2
        return 1
    fi

    if [[ $(grep -rlP "^\s*assert \(" $SOURCE_ROOT_PATH/nano) ]]; then
        echo "Using assert is not permitted. Use debug_assert instead." >&2
        return 1
    fi

    return 0
}

###################################################################################################
