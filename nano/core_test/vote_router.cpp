#include <nano/node/election.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/test_common/system.hpp>

#include <gtest/gtest.h>

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
