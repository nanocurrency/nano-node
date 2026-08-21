#include <nano/secure/rep_tiers.hpp>

nano::rep_tier nano::calculate_rep_tier (nano::uint128_t weight, nano::uint128_t online_stake)
{
	if (weight > online_stake / 20) // 5% or above (level 3)
	{
		return rep_tier::tier_3;
	}
	if (weight > online_stake / 100) // 1% or above (level 2)
	{
		return rep_tier::tier_2;
	}
	if (weight > online_stake / 1000) // 0.1% or above (level 1)
	{
		return rep_tier::tier_1;
	}
	return rep_tier::none;
}
