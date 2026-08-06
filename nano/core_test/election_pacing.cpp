#include <nano/node/election_behavior.hpp>
#include <nano/node/election_pacing.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
auto const base_latency = 100ms;
auto const vote_interval = 500ms;
auto const block_interval = 1000ms;

nano::election_pacing make_pacing ()
{
	return nano::election_pacing{ {
	.base_latency = base_latency,
	.vote_interval = vote_interval,
	.block_interval = block_interval,
	} };
}

// Start well past the clock epoch so interval arithmetic behaves like on a running system
std::chrono::steady_clock::time_point start_time ()
{
	return std::chrono::steady_clock::time_point{} + 1h;
}
}

TEST (election_pacing, vote_due_initially)
{
	auto pacing = make_pacing ();
	ASSERT_TRUE (pacing.due_vote (start_time ()));
}

TEST (election_pacing, vote_interval)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	pacing.vote_sent (now);
	ASSERT_FALSE (pacing.due_vote (now));
	ASSERT_FALSE (pacing.due_vote (now + vote_interval / 2));
	ASSERT_TRUE (pacing.due_vote (now + vote_interval));
	ASSERT_TRUE (pacing.due_vote (now + vote_interval * 2));
}

TEST (election_pacing, vote_reset)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	pacing.vote_sent (now);
	ASSERT_FALSE (pacing.due_vote (now));

	pacing.reset_vote ();
	ASSERT_TRUE (pacing.due_vote (now));
}

TEST (election_pacing, block_due_initially)
{
	auto pacing = make_pacing ();
	ASSERT_TRUE (pacing.due_block (nano::block_hash{ 1 }, start_time ()));
	ASSERT_TRUE (pacing.is_first_block ());
}

TEST (election_pacing, block_interval)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	nano::block_hash winner{ 1 };
	pacing.block_sent (winner, now);
	ASSERT_FALSE (pacing.is_first_block ());
	ASSERT_FALSE (pacing.due_block (winner, now));
	ASSERT_FALSE (pacing.due_block (winner, now + block_interval / 2));
	ASSERT_TRUE (pacing.due_block (winner, now + block_interval * 2));
}

TEST (election_pacing, block_winner_change)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	nano::block_hash winner1{ 1 };
	nano::block_hash winner2{ 2 };

	pacing.block_sent (winner1, now);
	ASSERT_FALSE (pacing.due_block (winner1, now));
	// A different winner is due immediately, regardless of the interval
	ASSERT_TRUE (pacing.due_block (winner2, now));

	pacing.block_sent (winner2, now);
	ASSERT_FALSE (pacing.due_block (winner2, now));
	ASSERT_TRUE (pacing.due_block (winner1, now));
}

TEST (election_pacing, request_due_initially)
{
	auto pacing = make_pacing ();
	ASSERT_TRUE (pacing.due_request (nano::election_behavior::priority, start_time ()));
}

TEST (election_pacing, request_interval)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	auto interval = pacing.request_interval (nano::election_behavior::priority);
	pacing.request_sent (now);
	ASSERT_FALSE (pacing.due_request (nano::election_behavior::priority, now));
	ASSERT_FALSE (pacing.due_request (nano::election_behavior::priority, now + interval / 2));
	ASSERT_TRUE (pacing.due_request (nano::election_behavior::priority, now + interval * 2));
}

TEST (election_pacing, request_interval_by_behavior)
{
	auto pacing = make_pacing ();

	ASSERT_EQ (base_latency * 5, pacing.request_interval (nano::election_behavior::manual));
	ASSERT_EQ (base_latency * 5, pacing.request_interval (nano::election_behavior::priority));
	ASSERT_EQ (base_latency * 5, pacing.request_interval (nano::election_behavior::hinted));
	ASSERT_EQ (base_latency * 2, pacing.request_interval (nano::election_behavior::optimistic));

	// Optimistic elections request less frequently, verify the behavior-dependent due time
	auto now = start_time ();
	pacing.request_sent (now);
	auto between = now + base_latency * 3;
	ASSERT_TRUE (pacing.due_request (nano::election_behavior::optimistic, between));
	ASSERT_FALSE (pacing.due_request (nano::election_behavior::priority, between));
}
