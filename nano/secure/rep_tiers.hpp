#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>

namespace nano
{
// Higher number means higher priority
enum class rep_tier
{
	none, // Not a principal representative
	tier_1, // (0.1-1%) of online stake
	tier_2, // (1-5%) of online stake
	tier_3, // (> 5%) of online stake
};

// Classify a representative's weight against the online stake
nano::rep_tier calculate_rep_tier (nano::uint128_t weight, nano::uint128_t online_stake);
}
