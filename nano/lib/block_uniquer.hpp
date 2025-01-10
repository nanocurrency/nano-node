#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/lib/uniquer.hpp>

namespace celerix
{
class block;
using block_uniquer = celerix::uniquer<celerix::uint256_union, celerix::block>;
}
