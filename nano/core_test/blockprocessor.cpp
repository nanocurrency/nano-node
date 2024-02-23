#include <nano/lib/blockbuilders.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (block_processor, broadcast_block_on_arrival)
{
	nano::test::system system;
	nano::node_config config1 = system.default_config ();
	// Deactivates elections on both nodes.
	config1.active_elections_size = 0;
	nano::node_config config2 = system.default_config ();
	config2.active_elections_size = 0;
	nano::node_flags flags;
	// Disables bootstrap listener to make sure the block won't be shared by this channel.
	flags.disable_bootstrap_listener = true;
	auto node1 = system.add_node (config1, flags);
	auto node2 = system.add_node (config2, flags);
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	// Adds a block to the first node. process_active() -> (calls) block_processor.add() -> add() ->
	// awakes process_block() -> process_batch() -> process_one() -> process_live()
	node1->process_active (send1);
	// Checks whether the block was broadcast.
	ASSERT_TIMELY (5s, node2->ledger.block_or_pruned_exists (send1->hash ()));
}
