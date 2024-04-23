#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (block_processor, broadcast_block_on_arrival)
{
	nano::test::system system;
	nano::node_config config1 = system.default_config ();
	// Deactivates elections on both nodes.
	config1.active_elections.size = 0;
	nano::node_config config2 = system.default_config ();
	config2.active_elections.size = 0;
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
	ASSERT_TIMELY (5s, node2->block_or_pruned_exists (send1->hash ()));
}

TEST (block_processor, rollback_overflow)
{
	nano::test::system system;
	nano::node_config config;
	config.priority_scheduler.depth = 1;
	auto node = system.add_node (config);
	nano::state_block_builder builder;
	nano::keypair key1;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node->ledger.process (node->ledger.tx_begin_write (), send1));
	node->ledger.confirm (node->ledger.tx_begin_write (), send1->hash ());
	nano::keypair key2;
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Gxrb_ratio)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node->ledger.process (node->ledger.tx_begin_write (), send2));
	node->ledger.confirm (node->ledger.tx_begin_write (), send2->hash ());

	auto open1 = builder.make_block ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::Gxrb_ratio)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	std::optional<nano::block_status> status;
	ASSERT_TRUE ((status = node->block_processor.add_blocking (open1, nano::block_source::live), status && status.value () == nano::block_status::progress));
	auto open2 = builder.make_block ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::Gxrb_ratio)
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (key2.pub))
				 .build ();
	ASSERT_TRUE ((status = node->block_processor.add_blocking (open2, nano::block_source::live), status && status.value () == nano::block_status::overflow));
}

TEST (block_processor, scheduler_confirmed_space)
{
	nano::test::system system;
	nano::node_config config;
	config.priority_scheduler.depth = 1;
	auto node = system.add_node (config);
	nano::state_block_builder builder;
	nano::keypair key1;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node->ledger.process (node->ledger.tx_begin_write (), send1));
	node->ledger.confirm (node->ledger.tx_begin_write (), send1->hash ());
	nano::keypair key2;
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Gxrb_ratio)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node->ledger.process (node->ledger.tx_begin_write (), send2));
	node->ledger.confirm (node->ledger.tx_begin_write (), send2->hash ());

	auto open1 = builder.make_block ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::Gxrb_ratio)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	std::optional<nano::block_status> status;
	ASSERT_TRUE ((status = node->block_processor.add_blocking (open1, nano::block_source::live), status && status.value () == nano::block_status::progress));
	auto election = node->active.election (open1->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (5s, node->active.empty ());
	auto open2 = builder.make_block ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::Gxrb_ratio)
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (key2.pub))
				 .build ();
	ASSERT_TRUE ((status = node->block_processor.add_blocking (open2, nano::block_source::live), status && status.value () == nano::block_status::progress));
}
