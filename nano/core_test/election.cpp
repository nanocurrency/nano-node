#include <nano/core_test/testutil.hpp>
#include <nano/node/election.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

TEST (election, construction)
{
	nano::system system (1);
	nano::genesis genesis;
	auto & node = *system.nodes[0];
	auto election = node.active.insert (genesis.open).election;
	ASSERT_TRUE (election->idle ());
	election->transition_active ();
	ASSERT_FALSE (election->idle ());
	election->transition_passive ();
	ASSERT_FALSE (election->idle ());
}
