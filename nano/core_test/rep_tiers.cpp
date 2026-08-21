#include <nano/lib/ratios.hpp>
#include <nano/secure/rep_tiers.hpp>

#include <gtest/gtest.h>

/*
 * calculate_rep_tier
 */

/*
 * The tier boundaries sit at 5%, 1% and 0.1% of the online stake and are all exclusive: a rep sitting exactly on a boundary still belongs to the lower tier.
 * This is the single definition of the boundaries shared by rep prioritization and vote cooldown, so these thresholds are load-bearing for both.
 */
TEST (rep_tiers, calculate_boundaries)
{
	// With an online stake of 1000 the boundaries sit at weight 50 (5%), 10 (1%) and 1 (0.1%)
	nano::uint128_t const online_stake{ 1000 };

	ASSERT_EQ (nano::rep_tier::tier_3, nano::calculate_rep_tier (51, online_stake));
	ASSERT_EQ (nano::rep_tier::tier_2, nano::calculate_rep_tier (50, online_stake)); // Exactly 5% stays tier_2
	ASSERT_EQ (nano::rep_tier::tier_2, nano::calculate_rep_tier (11, online_stake));
	ASSERT_EQ (nano::rep_tier::tier_1, nano::calculate_rep_tier (10, online_stake)); // Exactly 1% stays tier_1
	ASSERT_EQ (nano::rep_tier::tier_1, nano::calculate_rep_tier (2, online_stake));
	ASSERT_EQ (nano::rep_tier::none, nano::calculate_rep_tier (1, online_stake)); // Exactly 0.1% stays none
	ASSERT_EQ (nano::rep_tier::none, nano::calculate_rep_tier (0, online_stake));
}

/*
 * The same classification holds at real ledger magnitudes, where weights are 128-bit raw amounts, ensuring the arithmetic does not break down on large values.
 */
TEST (rep_tiers, calculate_realistic_scale)
{
	// Online stake of 100 million nano, expressed in raw
	nano::uint128_t const online_stake = 100'000'000 * nano::nano_ratio;

	ASSERT_EQ (nano::rep_tier::tier_3, nano::calculate_rep_tier (6'000'000 * nano::nano_ratio, online_stake)); // 6%
	ASSERT_EQ (nano::rep_tier::tier_2, nano::calculate_rep_tier (2'000'000 * nano::nano_ratio, online_stake)); // 2%
	ASSERT_EQ (nano::rep_tier::tier_1, nano::calculate_rep_tier (500'000 * nano::nano_ratio, online_stake)); // 0.5%
	ASSERT_EQ (nano::rep_tier::none, nano::calculate_rep_tier (1'000 * nano::nano_ratio, online_stake)); // 0.001%
}

/*
 * A weight above the online stake is possible when much of the network is offline; such a rep lands in the top tier rather than falling through the comparisons.
 */
TEST (rep_tiers, calculate_weight_above_online_stake)
{
	ASSERT_EQ (nano::rep_tier::tier_3, nano::calculate_rep_tier (5000, 1000));
}

/*
 * With zero online stake every threshold collapses to zero, so any rep with weight counts as top tier while a weightless rep still classifies as none.
 * This is the state at startup before any online weight has been measured.
 */
TEST (rep_tiers, calculate_zero_online_stake)
{
	ASSERT_EQ (nano::rep_tier::tier_3, nano::calculate_rep_tier (1, 0));
	ASSERT_EQ (nano::rep_tier::none, nano::calculate_rep_tier (0, 0));
}
