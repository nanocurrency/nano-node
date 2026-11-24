#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/uniquer.hpp>
#include <nano/secure/vote.hpp>

namespace nano
{
using block_uniquer = nano::uniquer<nano::uint256_union, nano::block>;
using vote_uniquer = nano::uniquer<nano::block_hash, nano::vote>;
}