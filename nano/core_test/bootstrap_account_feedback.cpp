#include <nano/lib/enum_util.hpp>
#include <nano/node/block_source.hpp>
#include <nano/node/bootstrap/account_feedback.hpp>
#include <nano/node/bootstrap/common.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

#include <deque>

using nano::bootstrap::account_feedback;
using nano::bootstrap::block_origin;
using nano::bootstrap::classify_feedback;
using nano::bootstrap::classify_origin;
using nano::bootstrap::strategy;

namespace
{
// Every submitter whose blocks are judged by their block source alone
std::deque<strategy> plain_submitters ()
{
	std::deque<strategy> result;
	for (auto const submitter : nano::enum_values<strategy> ())
	{
		if (submitter != strategy::ondemand)
		{
			result.push_back (submitter);
		}
	}
	return result;
}
}

/*
 * A block that entered the ledger raises the priority of its account, unless it arrived as live traffic:
 * the network delivers the rest of such an account on its own.
 */
TEST (bootstrap_account_feedback, progress)
{
	for (auto const submitter : plain_submitters ())
	{
		for (auto const source : nano::enum_values<nano::block_source> ())
		{
			auto const expected = source == nano::block_source::live ? account_feedback::none : account_feedback::progressed;
			ASSERT_EQ (classify_feedback (nano::block_status::progress, source, submitter), expected) << nano::to_string (source);
		}
	}
}

/*
 * A block with a missing source blocks its account only when bootstrap pulled it.
 * Live traffic must not be able to fill the blocked set, and no other source feeds it either.
 */
TEST (bootstrap_account_feedback, gap_source)
{
	for (auto const submitter : plain_submitters ())
	{
		for (auto const source : nano::enum_values<nano::block_source> ())
		{
			auto const expected = source == nano::block_source::bootstrap ? account_feedback::blocked : account_feedback::none;
			ASSERT_EQ (classify_feedback (nano::block_status::gap_source, source, submitter), expected) << nano::to_string (source);
		}
	}
}

/*
 * A block with a missing predecessor puts its account into the priority set only when it arrived as live traffic,
 * which is the sign that the network is ahead of the ledger on that account.
 */
TEST (bootstrap_account_feedback, gap_previous)
{
	for (auto const submitter : plain_submitters ())
	{
		for (auto const source : nano::enum_values<nano::block_source> ())
		{
			auto const expected = source == nano::block_source::live ? account_feedback::behind : account_feedback::none;
			ASSERT_EQ (classify_feedback (nano::block_status::gap_previous, source, submitter), expected) << nano::to_string (source);
		}
	}
}

/*
 * Every other result leaves the account sets alone, wherever the block came from and whoever submitted it.
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

		for (auto const submitter : nano::enum_values<strategy> ())
		{
			for (auto const source : nano::enum_values<nano::block_source> ())
			{
				ASSERT_EQ (classify_feedback (result, source, submitter), account_feedback::none) << nano::to_string (result) << " " << nano::to_string (source);
			}
		}
	}
}

/*
 * A block fetched on demand is treated like live traffic although it is submitted as a bootstrap block.
 * It fills a single hole in an account, so its progress is no reason to pull more of that account and its missing
 * source does not block the account, while a missing predecessor shows that the account is behind.
 */
TEST (bootstrap_account_feedback, ondemand_as_live)
{
	for (auto const source : nano::enum_values<nano::block_source> ())
	{
		ASSERT_EQ (classify_feedback (nano::block_status::progress, source, strategy::ondemand), account_feedback::none) << nano::to_string (source);
		ASSERT_EQ (classify_feedback (nano::block_status::gap_source, source, strategy::ondemand), account_feedback::none) << nano::to_string (source);
		ASSERT_EQ (classify_feedback (nano::block_status::gap_previous, source, strategy::ondemand), account_feedback::behind) << nano::to_string (source);
	}
}

/*
 * A block's origin follows its block source, except that a block fetched on demand counts as live whatever its source.
 */
TEST (bootstrap_account_feedback, origin)
{
	// On-demand blocks are live
	for (auto const source : nano::enum_values<nano::block_source> ())
	{
		ASSERT_EQ (classify_origin (source, strategy::ondemand), block_origin::live) << nano::to_string (source);
	}

	for (auto const submitter : plain_submitters ())
	{
		// Live and bootstrap sources keep their meaning
		ASSERT_EQ (classify_origin (nano::block_source::live, submitter), block_origin::live);
		ASSERT_EQ (classify_origin (nano::block_source::bootstrap, submitter), block_origin::bootstrap);

		// Every other source is neither
		for (auto const source : nano::enum_values<nano::block_source> ())
		{
			switch (source)
			{
				case nano::block_source::live:
				case nano::block_source::bootstrap:
					continue; // Checked above
				default:
					break;
			}

			ASSERT_EQ (classify_origin (source, submitter), block_origin::other) << nano::to_string (source);
		}
	}
}
