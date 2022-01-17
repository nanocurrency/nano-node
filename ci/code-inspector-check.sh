#!/usr/bin/env bash

###################################################################################################

source "$(dirname "$BASH_SOURCE")/impl/common.sh"

###################################################################################################

set -o errexit
set -o nounset

# This is to prevent out of scope access in async_write from asio which is not picked up by static analysers
if [[ $(grep -rl --exclude="*asio.hpp" "asio::async_write" $ROOTPATH/nano) ]]; then
    echo "Using boost::asio::async_write directly is not permitted (except in nano/lib/asio.hpp). Use nano::async_write instead"
    exit 1
fi

# prevent unsolicited use of std::lock_guard, std::unique_lock, std::condition_variable & std::mutex outside of allowed areas
if [[ $(grep -rl --exclude={"*random_pool.cpp","*random_pool.hpp","*random_pool_shuffle.hpp","*locks.hpp","*locks.cpp"} "std::unique_lock\|std::lock_guard\|std::condition_variable\|std::mutex" $ROOTPATH/nano) ]]; then
    echo "Using std::unique_lock, std::lock_guard, std::condition_variable or std::mutex is not permitted (except in nano/lib/locks.hpp and non-nano dependent libraries). Use the nano::* versions instead"
    exit 1
fi

if [[ $(grep -rlP "^\s*assert \(" $ROOTPATH/nano) ]]; then
    echo "Using assert is not permitted. Use debug_assert instead."
    exit 1
fi

echo "code-inspector check passed"


###################################################################################################
