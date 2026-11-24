#include <nano/lib/constants.hpp>
#include <nano/node/election_pacing.hpp>
#include <nano/secure/network_params.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace
{
auto const base_latency = 100ms;
auto const vote_interval = 500ms;
auto const flood_interval = 300ms;
auto const block_interval = 1000ms;
auto const block_delay = 200ms;
auto const request_interval = base_latency * 15;

nano::election_pacing make_pacing ()
{
	return nano::election_pacing{ {
	.base_latency = base_latency,
	.vote_interval = vote_interval,
	.flood_interval = flood_interval,
	.block_interval = block_interval,
	.block_delay = block_delay,
	.request_interval = request_interval,
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

TEST (election_pacing, flood_due_initially)
{
	auto pacing = make_pacing ();
	ASSERT_TRUE (pacing.due_flood (nano::block_hash{ 1 }, start_time ()));
	ASSERT_TRUE (pacing.is_first_flood ());
	ASSERT_FALSE (pacing.due_directed (nano::block_hash{ 1 }, start_time ()));
}

/*
 * A flooded winner is not flooded again until the flood interval has passed
 */
TEST (election_pacing, flood_interval)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	// The first flood of the winner
	nano::block_hash winner{ 1 };
	pacing.flood_sent (winner, now);
	ASSERT_FALSE (pacing.is_first_flood ());

	// Held back within the flood interval, due again past it
	ASSERT_FALSE (pacing.due_flood (winner, now));
	ASSERT_FALSE (pacing.due_flood (winner, now + flood_interval / 2));
	ASSERT_TRUE (pacing.due_flood (winner, now + flood_interval * 2));
}

TEST (election_pacing, flood_winner_change)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	nano::block_hash winner1{ 1 };
	nano::block_hash winner2{ 2 };

	pacing.flood_sent (winner1, now);
	ASSERT_FALSE (pacing.due_flood (winner1, now));
	// A different winner is due immediately, regardless of the interval
	ASSERT_TRUE (pacing.due_flood (winner2, now));

	pacing.flood_sent (winner2, now);
	ASSERT_FALSE (pacing.due_flood (winner2, now));
	ASSERT_TRUE (pacing.due_flood (winner1, now));
}

/*
 * An election that never requested votes is due for its first request at once
 */
TEST (election_pacing, request_due_initially)
{
	auto pacing = make_pacing ();
	ASSERT_TRUE (pacing.due_request (start_time ()));
}

/*
 * After the first request the next one waits for the request interval, which starts at the base latency
 */
TEST (election_pacing, request_interval)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	// The first request, the gap to the next one is a single base latency
	pacing.request_sent (now);
	auto const interval = pacing.request_interval ();
	ASSERT_EQ (base_latency, interval);

	// Held back within the interval, due again past it
	ASSERT_FALSE (pacing.due_request (now));
	ASSERT_FALSE (pacing.due_request (now + interval / 2));
	ASSERT_TRUE (pacing.due_request (now + interval * 2));
}

/*
 * The directed broadcast follows the winner's flood after the block delay, then the block interval applies, and a new winner waits for its own flood
 */
TEST (election_pacing, directed_after_flood)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	nano::block_hash winner{ 1 };
	pacing.flood_sent (winner, now);
	ASSERT_FALSE (pacing.due_directed (winner, now));
	ASSERT_FALSE (pacing.due_directed (winner, now + block_delay / 2));
	ASSERT_TRUE (pacing.due_directed (winner, now + block_delay));

	// Sent, the next one waits for the block interval
	pacing.directed_sent (winner, now + block_delay);
	ASSERT_FALSE (pacing.due_directed (winner, now + block_delay * 2));
	ASSERT_TRUE (pacing.due_directed (winner, now + block_delay + block_interval * 2));

	// A new winner is not sent to the reps before its own flood had the delay to spread
	nano::block_hash winner2{ 2 };
	ASSERT_FALSE (pacing.due_directed (winner2, now + block_delay * 2));
	pacing.flood_sent (winner2, now + block_delay * 2);
	ASSERT_FALSE (pacing.due_directed (winner2, now + block_delay * 2));
	ASSERT_TRUE (pacing.due_directed (winner2, now + block_delay * 3));
}

/*
 * Floods and directed broadcasts of the same winner repeat on their own intervals, whichever of the two is shorter
 */
TEST (election_pacing, flood_directed_intervals_independent)
{
	// One flood and one directed broadcast at the same times in each case, checked past the shorter interval and short of the longer one
	nano::block_hash winner{ 1 };
	auto const flood_time = start_time ();
	auto const directed_time = flood_time + block_delay;
	auto const check_time = directed_time + flood_interval * 2;
	ASSERT_LT (check_time, flood_time + block_interval);

	// The flood interval is the shorter one, so the flood is due again while the directed broadcast still waits
	{
		auto pacing = make_pacing ();
		pacing.flood_sent (winner, flood_time);
		pacing.directed_sent (winner, directed_time);
		ASSERT_TRUE (pacing.due_flood (winner, check_time));
		ASSERT_FALSE (pacing.due_directed (winner, check_time));
	}

	// With the two intervals swapped the directed broadcast is due again while the flood still waits
	{
		nano::election_pacing pacing{ {
		.base_latency = base_latency,
		.vote_interval = vote_interval,
		.flood_interval = block_interval,
		.block_interval = flood_interval,
		.block_delay = block_delay,
		.request_interval = request_interval,
		} };
		pacing.flood_sent (winner, flood_time);
		pacing.directed_sent (winner, directed_time);
		ASSERT_FALSE (pacing.due_flood (winner, check_time));
		ASSERT_TRUE (pacing.due_directed (winner, check_time));
	}
}

/*
 * The gap after each confirmation request starts at one base latency and doubles with every request that went out, until it reaches the configured request interval
 */
TEST (election_pacing, request_backoff)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	// Gaps of 1, 2, 4 and 8 base latencies, then the configured 15 from there on
	ASSERT_EQ (base_latency * 15, request_interval);
	std::vector<std::chrono::milliseconds> const gaps{ base_latency, base_latency * 2, base_latency * 4, base_latency * 8, request_interval, request_interval };
	for (auto const & gap : gaps)
	{
		// Each request goes out once due, the next one waits for the gap and becomes due right after it
		ASSERT_TRUE (pacing.due_request (now));
		pacing.request_sent (now);
		ASSERT_EQ (gap, pacing.request_interval ());
		ASSERT_FALSE (pacing.due_request (now + gap));
		now += gap + 1ms;
	}
}

/*
 * An election that keeps requesting stays at the configured request interval, with far more requests than doublings the gap could hold
 */
TEST (election_pacing, request_backoff_capped)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	// A thousand requests, unbounded doubling would have overflowed the interval long before
	for (int i = 0; i < 1000; ++i)
	{
		pacing.request_sent (now);
	}

	// The gap is the configured request interval, the next request is due right after it
	ASSERT_EQ (request_interval, pacing.request_interval ());
	ASSERT_FALSE (pacing.due_request (now + request_interval));
	ASSERT_TRUE (pacing.due_request (now + request_interval + 1ms));
}

/*
 * A due request that did not go out leaves the interval as it was, only requests recorded as sent double it
 */
TEST (election_pacing, request_backoff_not_sent)
{
	auto pacing = make_pacing ();
	auto now = start_time ();

	// After the first request the next one is due once a base latency passed
	pacing.request_sent (now);
	ASSERT_EQ (base_latency, pacing.request_interval ());
	ASSERT_TRUE (pacing.due_request (now + base_latency * 2));

	// Nothing went out, so the request stays due and the interval does not grow however long it waits
	ASSERT_TRUE (pacing.due_request (now + base_latency * 20));
	ASSERT_EQ (base_latency, pacing.request_interval ());

	// The next request that goes out doubles it
	pacing.request_sent (now + base_latency * 20);
	ASSERT_EQ (base_latency * 2, pacing.request_interval ());
}

/*
 * Pacing built from the network constants takes each interval from its own constant
 */
TEST (election_pacing, network_constants)
{
	// The fixture's values, all distinct, so a constant feeding the wrong interval shows up
	auto network = nano::dev::network_params.network;
	network.election_base_latency = base_latency;
	network.vote_broadcast_interval = vote_interval;
	network.block_flood_interval = flood_interval;
	network.block_broadcast_interval = block_interval;
	network.block_broadcast_delay = block_delay;
	network.election_request_interval = request_interval;
	nano::election_pacing pacing{ network };
	auto now = start_time ();
	nano::block_hash winner{ 1 };

	// Votes repeat after the vote interval
	pacing.vote_sent (now);
	ASSERT_FALSE (pacing.due_vote (now + vote_interval - 1ms));
	ASSERT_TRUE (pacing.due_vote (now + vote_interval));

	// Floods repeat after the flood interval
	pacing.flood_sent (winner, now);
	ASSERT_FALSE (pacing.due_flood (winner, now + flood_interval));
	ASSERT_TRUE (pacing.due_flood (winner, now + flood_interval + 1ms));

	// The directed broadcast waits for the block delay after the flood, then repeats after the block interval
	ASSERT_FALSE (pacing.due_directed (winner, now + block_delay - 1ms));
	ASSERT_TRUE (pacing.due_directed (winner, now + block_delay));
	pacing.directed_sent (winner, now + block_delay);
	ASSERT_FALSE (pacing.due_directed (winner, now + block_delay + block_interval));
	ASSERT_TRUE (pacing.due_directed (winner, now + block_delay + block_interval + 1ms));

	// The first request gap is the base latency, the gaps stop growing at the request interval
	pacing.request_sent (now);
	ASSERT_EQ (base_latency, pacing.request_interval ());
	for (int i = 0; i < 10; ++i)
	{
		pacing.request_sent (now);
	}
	ASSERT_EQ (request_interval, pacing.request_interval ());
}
