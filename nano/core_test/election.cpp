#include <nano/core_test/testutil.hpp>
#include <nano/node/election.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

TEST (election, construction)
{
	nano::system system (1);
	nano::genesis genesis;
	auto & node = *system.nodes[0];
	node.active.start (genesis.open);
	auto election = node.active.election (genesis.open->qualified_root ());
	ASSERT_FALSE (election->idle ());
	election->transition_idle ();
	ASSERT_TRUE (election->idle ());
	election->transition_active ();
	ASSERT_FALSE (election->idle ());
	election->transition_idle ();
	ASSERT_TRUE (election->idle ());
}
