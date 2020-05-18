#include <nano/core_test/testutil.hpp>
#include <nano/node/election.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

TEST (election, construction)
{
	nano::system system (1);
	nano::genesis genesis;
	auto & node = *system.nodes[0];
	genesis.open->sideband_set (nano::block_sideband (nano::genesis_account, 0, nano::genesis_amount, 1, nano::seconds_since_epoch (), nano::epoch::epoch_0, false, false, false));
	auto election = node.active.insert (genesis.open).election;
	ASSERT_TRUE (election->idle ());
	election->transition_active ();
	ASSERT_FALSE (election->idle ());
	election->transition_passive ();
	ASSERT_FALSE (election->idle ());
}

namespace nano
{
TEST (election, bisect_dependencies)
{
	nano::system system;
	nano::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);
	nano::genesis genesis;
	nano::confirmation_height_info conf_info;
	ASSERT_FALSE (node.store.confirmation_height_get (node.store.tx_begin_read (), nano::test_genesis_key.pub, conf_info));
	ASSERT_EQ (1, conf_info.height);
	std::vector<std::shared_ptr<nano::block>> blocks;
	blocks.push_back (nullptr); // idx == height
	blocks.push_back (genesis.open);
	nano::block_builder builder;
	auto amount = nano::genesis_amount;
	for (int i = 0; i < 29; ++i)
	{
		auto latest = blocks.back ();
		blocks.push_back (builder.state ()
		                  .previous (latest->hash ())
		                  .account (nano::test_genesis_key.pub)
		                  .representative (nano::test_genesis_key.pub)
		                  .balance (--amount)
		                  .link (nano::test_genesis_key.pub)
		                  .sign (nano::test_genesis_key.prv, nano::test_genesis_key.pub)
		                  .work (*system.work.generate (latest->hash ()))
		                  .build ());
		ASSERT_EQ (nano::process_result::progress, node.process (*blocks.back ()).code);
	}
	ASSERT_EQ (31, blocks.size ());
	ASSERT_TRUE (node.active.empty ());
	{
		auto election = node.active.insert (blocks.back ()).election;
		ASSERT_NE (nullptr, election);
		ASSERT_EQ (30, election->blocks.begin ()->second->sideband ().height);
		nano::lock_guard<std::mutex> guard (node.active.mutex);
		election->activate_dependencies ();
	}
	auto check_height_and_activate_next = [&node, &blocks](uint64_t height_a) {
		auto election = node.active.election (blocks[height_a]->qualified_root ());
		ASSERT_NE (nullptr, election);
		ASSERT_EQ (height_a, election->blocks.begin ()->second->sideband ().height);
		nano::lock_guard<std::mutex> guard (node.active.mutex);
		election->activate_dependencies ();
	};
	ASSERT_EQ (2, node.active.size ());
	check_height_and_activate_next (15);
	ASSERT_EQ (3, node.active.size ());
	check_height_and_activate_next (8);
	ASSERT_EQ (4, node.active.size ());
	check_height_and_activate_next (4);
	ASSERT_EQ (5, node.active.size ());
	check_height_and_activate_next (2);
	ASSERT_EQ (5, node.active.size ());
	ASSERT_EQ (node.active.blocks.size (), node.active.roots.size ());
}
}
