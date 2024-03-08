#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/uniquer.hpp>

namespace nano
{
class block;
using block_uniquer = nano::uniquer<nano::uint256_union, nano::block>;
}
