#include <nano/lib/vote.hpp>
#include <nano/node/election.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

/*
 * Disconnecting an election removes every route that currently refers to it.
 * Reassigning a hash changes that ownership, so the route must survive when its former election is disconnected.
 */
TEST (vote_router, disconnect_election)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto election1 = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);
	auto election2 = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);
	nano::block_hash const hash1{ 1 };
	nano::block_hash const hash2{ 2 };
	nano::block_hash const hash3{ 3 };

	node.vote_router.connect (hash1, election1);
	node.vote_router.connect (hash2, election1);
	node.vote_router.connect (hash3, election2);

	// Reassign hash2 before disconnecting its original election
	node.vote_router.connect (hash2, election2);
	node.vote_router.disconnect (election1);
	ASSERT_FALSE (node.vote_router.contains (hash1));
	ASSERT_TRUE (node.vote_router.contains (hash2));
	ASSERT_TRUE (node.vote_router.contains (hash3));

	node.vote_router.disconnect (election2);
	ASSERT_FALSE (node.vote_router.contains (hash2));
	ASSERT_FALSE (node.vote_router.contains (hash3));
}

/*
 * A hash-scoped disconnect removes the route only when it points to the given election.
 */
TEST (vote_router, disconnect_hash_checks_owner)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto election1 = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);
	auto election2 = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);
	nano::block_hash const hash{ 1 };

	ASSERT_TRUE (node.vote_router.connect (hash, election1));

	// Another election cannot remove a route it does not own
	ASSERT_FALSE (node.vote_router.disconnect (hash, election2));
	ASSERT_EQ (election1, node.vote_router.election (hash));

	// The owner removes it, a repeated attempt finds nothing
	ASSERT_TRUE (node.vote_router.disconnect (hash, election1));
	ASSERT_FALSE (node.vote_router.contains (hash));
	ASSERT_FALSE (node.vote_router.disconnect (hash, election1));
}

/*
 * A hash belongs to a single election, so two elections claiming it is a conflict.
 * It resolves to the election started later regardless of the order in which they connect.
 */
TEST (vote_router, connect_prefers_later_election)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto older = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);
	std::this_thread::sleep_for (1ms);
	auto newer = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);
	ASSERT_LT (older->get_election_start (), newer->get_election_start ());
	nano::block_hash const hash{ 1 };

	// The later election takes over a route held by the earlier one
	ASSERT_TRUE (node.vote_router.connect (hash, older));
	ASSERT_TRUE (node.vote_router.connect (hash, newer));
	ASSERT_EQ (newer, node.vote_router.election (hash));

	// The earlier election cannot take it back
	ASSERT_FALSE (node.vote_router.connect (hash, older));
	ASSERT_EQ (newer, node.vote_router.election (hash));

	// Reconnecting the current election keeps the route
	ASSERT_TRUE (node.vote_router.connect (hash, newer));
	ASSERT_EQ (newer, node.vote_router.election (hash));
}

/*
 * A route whose election no longer exists yields to any election, however early it started.
 */
TEST (vote_router, connect_replaces_expired_route)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto older = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);
	nano::block_hash const hash{ 1 };
	{
		std::this_thread::sleep_for (1ms);
		auto newer = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);
		ASSERT_TRUE (node.vote_router.connect (hash, newer));
	}
	ASSERT_EQ (nullptr, node.vote_router.election (hash));

	ASSERT_TRUE (node.vote_router.connect (hash, older));
	ASSERT_EQ (older, node.vote_router.election (hash));
}

/*
 * A vote for a hash that no election claims is cached and reported as undetermined.
 */
TEST (vote_router, vote_unmatched_is_cached)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::block_hash const hash{ 1 };

	auto vote = nano::test::make_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ hash }, 1);
	auto results = node.vote_router.vote (vote);
	ASSERT_EQ (nano::vote_code::indeterminate, results.at (hash));
	ASSERT_EQ (1, node.vote_cache.find (hash).size ());
	ASSERT_FALSE (node.vote_router.contains (hash));
}

/*
 * A vote reaches an election that starts while the vote is being routed.
 * A starting election connects its route and then reads the vote cache, so a vote that found no route must not be cached too late for that read and then left there.
 */
TEST (vote_router, vote_during_election_start)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::block_hash const routed{ 1 };
	nano::block_hash const starting{ 2 };
	auto started = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);

	// Runs while the router votes on the routed hash, after it found no route for the other hash and before it caches the vote
	auto start_election = [&] (nano::account const &) {
		ASSERT_TRUE (node.vote_router.connect (starting, started));
		ASSERT_TRUE (node.vote_cache.find (starting).empty ());
	};
	auto voting = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0, nullptr, start_election);
	ASSERT_TRUE (node.vote_router.connect (routed, voting));

	auto vote = nano::test::make_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ routed, starting }, 1);
	auto results = node.vote_router.vote (vote);
	ASSERT_EQ (nano::vote_code::vote, results.at (routed));
	ASSERT_EQ (nano::vote_code::vote, results.at (starting));

	auto votes = started->votes ();
	ASSERT_TRUE (votes.contains (nano::dev::genesis_key.pub));
	ASSERT_EQ (starting, votes.at (nano::dev::genesis_key.pub).hash);

	// The vote stays cached for an election that starts later
	ASSERT_EQ (1, node.vote_cache.find (starting).size ());
}

/*
 * An election that starts while a vote is being routed may have received that vote from the vote cache by the time the router looks again.
 * The second delivery is not a replay by the representative, so the hash is still reported as unmatched.
 */
TEST (vote_router, vote_during_election_start_already_delivered)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::block_hash const routed{ 1 };
	nano::block_hash const starting{ 2 };
	auto started = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0);
	auto vote = nano::test::make_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ routed, starting }, 1);

	// Runs while the router votes on the routed hash, the starting election gets the vote the way a vote cache read delivers it
	auto start_election = [&] (nano::account const &) {
		ASSERT_TRUE (node.vote_router.connect (starting, started));
		ASSERT_EQ (nano::vote_code::vote, started->vote (vote->account, vote->timestamp (), starting, nano::vote_source::cache));
	};
	auto voting = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 0, nullptr, start_election);
	ASSERT_TRUE (node.vote_router.connect (routed, voting));

	auto results = node.vote_router.vote (vote);
	ASSERT_EQ (nano::vote_code::vote, results.at (routed));
	ASSERT_EQ (nano::vote_code::indeterminate, results.at (starting));
	ASSERT_EQ (1, started->votes ().count (nano::dev::genesis_key.pub));
}
