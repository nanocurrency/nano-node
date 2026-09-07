#include <nano/node/election.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/test_common/system.hpp>

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
