#include <nano/lib/enum_util.hpp>
#include <nano/node/block_source.hpp>
#include <nano/node/bootstrap/account_feedback.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

using nano::bootstrap::account_feedback;
using nano::bootstrap::block_origin;
using nano::bootstrap::classify_feedback;
using nano::bootstrap::classify_origin;

/*
 * A block that entered the ledger raises the priority of its account, unless it arrived as live traffic:
 * the network delivers the rest of such an account on its own.
 */
TEST (bootstrap_account_feedback, progress)
{
	for (auto const source : nano::enum_values<nano::block_source> ())
	{
		auto const expected = classify_origin (source) == block_origin::live ? account_feedback::none : account_feedback::progressed;
		ASSERT_EQ (classify_feedback (nano::block_status::progress, source), expected) << nano::to_string (source);
	}
}

/*
 * A block with a missing source blocks its account only when bootstrap pulled it.
 * Live traffic must not be able to fill the blocked set, and no other source feeds it either.
 */
TEST (bootstrap_account_feedback, gap_source)
{
	for (auto const source : nano::enum_values<nano::block_source> ())
	{
		auto const expected = classify_origin (source) == block_origin::bootstrap ? account_feedback::blocked : account_feedback::none;
		ASSERT_EQ (classify_feedback (nano::block_status::gap_source, source), expected) << nano::to_string (source);
	}
}

/*
 * A block with a missing predecessor puts its account into the priority set only when it arrived as live traffic,
 * which is the sign that the network is ahead of the ledger on that account.
 */
TEST (bootstrap_account_feedback, gap_previous)
{
	for (auto const source : nano::enum_values<nano::block_source> ())
	{
		auto const expected = classify_origin (source) == block_origin::live ? account_feedback::behind : account_feedback::none;
		ASSERT_EQ (classify_feedback (nano::block_status::gap_previous, source), expected) << nano::to_string (source);
	}
}

/*
 * Every other result leaves the account sets alone, wherever the block came from.
 */
TEST (bootstrap_account_feedback, other_results)
{
	for (auto const result : nano::enum_values<nano::block_status> ())
	{
		switch (result)
		{
			case nano::block_status::progress:
			case nano::block_status::gap_source:
			case nano::block_status::gap_previous:
				continue; // The results with feedback have their own tests
			default:
				break;
		}

		for (auto const source : nano::enum_values<nano::block_source> ())
		{
			ASSERT_EQ (classify_feedback (result, source), account_feedback::none) << nano::to_string (result) << " " << nano::to_string (source);
		}
	}
}

/*
 * Live traffic is what the network floods, whether a block comes from the node that created it or from one passing it on.
 * Bootstrap pulls are their own origin, and every other source is neither.
 */
TEST (bootstrap_account_feedback, origin)
{
	// Both kinds of flooded blocks are live
	ASSERT_EQ (classify_origin (nano::block_source::live), block_origin::live);
	ASSERT_EQ (classify_origin (nano::block_source::live_originator), block_origin::live);

	// Pulled blocks are bootstrap
	ASSERT_EQ (classify_origin (nano::block_source::bootstrap), block_origin::bootstrap);

	// Every other source is neither
	for (auto const source : nano::enum_values<nano::block_source> ())
	{
		switch (source)
		{
			case nano::block_source::live:
			case nano::block_source::live_originator:
			case nano::block_source::bootstrap:
				continue; // Checked above
			default:
				break;
		}

		ASSERT_EQ (classify_origin (source), block_origin::other) << nano::to_string (source);
	}
}

/*
 * A block received from the node that created it gets the feedback of any other live block: its progress is no reason
 * to pull more of its account, a missing predecessor shows that the account is behind, and a missing source blocks nothing.
 */
TEST (bootstrap_account_feedback, originator_as_live)
{
	auto const source = nano::block_source::live_originator;

	ASSERT_EQ (classify_feedback (nano::block_status::progress, source), account_feedback::none);
	ASSERT_EQ (classify_feedback (nano::block_status::gap_previous, source), account_feedback::behind);
	ASSERT_EQ (classify_feedback (nano::block_status::gap_source, source), account_feedback::none);
}
