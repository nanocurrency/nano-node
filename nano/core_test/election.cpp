#include <nano/node/election.hpp>
#include <nano/node/testing.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (election, construction)
{
	nano::system system (1);
	nano::genesis genesis;
	auto & node = *system.nodes[0];
	genesis.open->sideband_set (nano::block_sideband (nano::genesis_account, 0, nano::genesis_amount, 1, nano::seconds_since_epoch (), nano::epoch::epoch_0, false, false, false, nano::epoch::epoch_0));
	auto election = node.active.insert (genesis.open).election;
	election->transition_active ();
}
