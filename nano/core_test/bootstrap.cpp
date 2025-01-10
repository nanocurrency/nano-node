#include <celerix/lib/blocks.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/tomlconfig.hpp>
#include <celerix/node/bootstrap/bootstrap_service.hpp>
#include <celerix/node/bootstrap/database_scan.hpp>
#include <celerix/node/make_store.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/test_common/chains.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <sstream>

using namespace std::chrono_literals;

namespace
{
celerix::block_hash random_hash ()
{
	celerix::block_hash random_hash;
	celerix::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	return random_hash;
}
}

/*
 * account_sets
 */

TEST (account_sets, construction)
{
	celerix::test::system system;
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
}

TEST (account_sets, empty_blocked)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	ASSERT_FALSE (sets.blocked (account));
}

TEST (account_sets, block)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	sets.priority_up (account);
	sets.block (account, random_hash ());
	ASSERT_TRUE (sets.blocked (account));
}

TEST (account_sets, unblock)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	auto hash = random_hash ();
	sets.priority_up (account);
	sets.block (account, hash);
	ASSERT_TRUE (sets.blocked (account));
	sets.unblock (account, hash);
	ASSERT_FALSE (sets.blocked (account));
}

TEST (account_sets, priority_base)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	ASSERT_EQ (0.0, sets.priority (account));
}

TEST (account_sets, priority_blocked)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	sets.block (account, random_hash ());
	ASSERT_EQ (0.0, sets.priority (account));
}

TEST (account_sets, priority_unblock)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	sets.priority_up (account);
	ASSERT_EQ (sets.priority (account), celerix::bootstrap::account_sets::priority_initial);
	auto hash = random_hash ();
	sets.block (account, hash);
	ASSERT_EQ (0.0, sets.priority (account));
	sets.unblock (account, hash);
	ASSERT_EQ (sets.priority (account), celerix::bootstrap::account_sets::priority_initial);
}

TEST (account_sets, priority_up_down)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	sets.priority_up (account);
	ASSERT_EQ (sets.priority (account), celerix::bootstrap::account_sets::priority_initial);
	sets.priority_down (account);
	ASSERT_EQ (sets.priority (account), celerix::bootstrap::account_sets::priority_initial / celerix::bootstrap::account_sets::priority_divide);
}

TEST (account_sets, priority_down_empty)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	sets.priority_down (account);
	ASSERT_EQ (0.0, sets.priority (account));
}

TEST (account_sets, priority_down_saturate)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	sets.priority_up (account);
	ASSERT_EQ (sets.priority (account), celerix::bootstrap::account_sets::priority_initial);
	for (int n = 0; n < 1000; ++n)
	{
		sets.priority_down (account);
	}
	ASSERT_FALSE (sets.prioritized (account));
}

TEST (account_sets, priority_set)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	sets.priority_set (account, 10.0);
	ASSERT_EQ (sets.priority (account), 10.0);
}

// Ensure priority value is bounded
TEST (account_sets, saturate_priority)
{
	celerix::test::system system;

	celerix::account account{ 1 };
	celerix::account_sets_config config;
	celerix::bootstrap::account_sets sets{ config, system.stats };
	for (int n = 0; n < 1000; ++n)
	{
		sets.priority_up (account);
	}
	ASSERT_EQ (sets.priority (account), celerix::bootstrap::account_sets::priority_max);
}

/*
 * bootstrap
 */

/**
 * Tests the base case for returning
 */
TEST (bootstrap, account_base)
{
	celerix::node_flags flags;
	celerix::test::system system{ 1, celerix::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .link (0)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node0.process (send1));
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()) != nullptr);
}

/**
 * Tests that bootstrap will return multiple new blocks in-order
 */
TEST (bootstrap, account_inductive)
{
	celerix::node_flags flags;
	celerix::test::system system{ 1, celerix::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .link (0)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .link (0)
				 .balance (celerix::dev::constants.genesis_amount - 2)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	//	std::cerr << "Genesis: " << celerix::dev::genesis->hash ().to_string () << std::endl;
	//	std::cerr << "Send1: " << send1->hash ().to_string () << std::endl;
	//	std::cerr << "Send2: " << send2->hash ().to_string () << std::endl;
	ASSERT_EQ (celerix::block_status::progress, node0.process (send1));
	ASSERT_EQ (celerix::block_status::progress, node0.process (send2));
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (50s, node1.block (send2->hash ()) != nullptr);
}

/**
 * Tests that bootstrap will return multiple new blocks in-order
 */
TEST (bootstrap, trace_base)
{
	celerix::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	celerix::test::system system{ 1, celerix::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	celerix::keypair key;
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto receive1 = builder.make_block ()
					.account (key.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.link (send1->hash ())
					.balance (1)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build ();
	//	std::cerr << "Genesis key: " << celerix::dev::genesis_key.pub.to_account () << std::endl;
	//	std::cerr << "Key: " << key.pub.to_account () << std::endl;
	//	std::cerr << "Genesis: " << celerix::dev::genesis->hash ().to_string () << std::endl;
	//	std::cerr << "send1: " << send1->hash ().to_string () << std::endl;
	//	std::cerr << "receive1: " << receive1->hash ().to_string () << std::endl;
	auto & node1 = *system.add_node ();
	//	std::cerr << "--------------- Start ---------------\n";
	ASSERT_EQ (celerix::block_status::progress, node0.process (send1));
	ASSERT_EQ (celerix::block_status::progress, node0.process (receive1));
	ASSERT_EQ (node1.ledger.any.receivable_end (), node1.ledger.any.receivable_upper_bound (node1.ledger.tx_begin_read (), key.pub, 0));
	//	std::cerr << "node0: " << node0.network.endpoint () << std::endl;
	//	std::cerr << "node1: " << node1.network.endpoint () << std::endl;
	ASSERT_TIMELY (10s, node1.block (receive1->hash ()) != nullptr);
}

/*
 * Tests that bootstrap will prioritize existing accounts with outdated frontiers
 */
TEST (bootstrap, frontier_scan)
{
	celerix::test::system system;

	celerix::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	celerix::node_config config;
	// Disable other bootstrap strategies
	config.bootstrap.enable_scan = false;
	config.bootstrap.enable_dependency_walker = false;
	// Disable election activation
	config.backlog_scan.enable = false;
	config.priority_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;
	config.hinted_scheduler.enable = false;

	// Prepare blocks for frontier scan (genesis 10 sends -> 10 opens -> 10 updates)
	std::vector<std::shared_ptr<celerix::block>> sends;
	std::vector<std::shared_ptr<celerix::block>> opens;
	std::vector<std::shared_ptr<celerix::block>> updates;
	{
		auto source = celerix::dev::genesis_key;
		auto latest = celerix::dev::genesis->hash ();
		auto balance = celerix::dev::genesis->balance ().number ();

		size_t const count = 10;

		for (int n = 0; n < count; ++n)
		{
			celerix::keypair key;
			celerix::block_builder builder;

			balance -= 1;
			auto send = builder
						.state ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.balance (balance)
						.link (key.pub)
						.sign (source.prv, source.pub)
						.work (*system.work.generate (latest))
						.build ();

			latest = send->hash ();

			auto open = builder
						.state ()
						.account (key.pub)
						.previous (0)
						.representative (key.pub)
						.balance (1)
						.link (send->hash ())
						.sign (key.prv, key.pub)
						.work (*system.work.generate (key.pub))
						.build ();

			auto update = builder
						  .state ()
						  .account (key.pub)
						  .previous (open->hash ())
						  .representative (0)
						  .balance (1)
						  .link (0)
						  .sign (key.prv, key.pub)
						  .work (*system.work.generate (open->hash ()))
						  .build ();

			sends.push_back (send);
			opens.push_back (open);
			updates.push_back (update);
		}
	}

	// Initialize nodes with blocks without the `updates` frontiers
	std::vector<std::shared_ptr<celerix::block>> blocks;
	blocks.insert (blocks.end (), sends.begin (), sends.end ());
	blocks.insert (blocks.end (), opens.begin (), opens.end ());
	system.set_initialization_blocks ({ blocks.begin (), blocks.end () });

	auto & node0 = *system.add_node (config, flags);
	ASSERT_TRUE (celerix::test::process (node0, updates));

	// No blocks should be broadcast to the other node
	auto & node1 = *system.add_node (config, flags);
	ASSERT_ALWAYS_EQ (100ms, node1.ledger.block_count (), blocks.size () + 1);

	// Frontier scan should detect all the accounts with missing blocks
	ASSERT_TIMELY (10s, std::all_of (updates.begin (), updates.end (), [&node1] (auto const & block) {
		return node1.bootstrap.prioritized (block->account ());
	}));
}

/*
 * Tests that bootstrap will prioritize not yet existing accounts with pending blocks
 */
TEST (bootstrap, frontier_scan_pending)
{
	celerix::test::system system;

	celerix::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	celerix::node_config config;
	// Disable other bootstrap strategies
	config.bootstrap.enable_scan = false;
	config.bootstrap.enable_dependency_walker = false;
	// Disable election activation
	config.backlog_scan.enable = false;
	config.priority_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;
	config.hinted_scheduler.enable = false;

	// Prepare blocks for frontier scan (genesis 10 sends -> 10 opens)
	std::vector<std::shared_ptr<celerix::block>> sends;
	std::vector<std::shared_ptr<celerix::block>> opens;
	{
		auto source = celerix::dev::genesis_key;
		auto latest = celerix::dev::genesis->hash ();
		auto balance = celerix::dev::genesis->balance ().number ();

		size_t const count = 10;

		for (int n = 0; n < count; ++n)
		{
			celerix::keypair key;
			celerix::block_builder builder;

			balance -= 1;
			auto send = builder
						.state ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.balance (balance)
						.link (key.pub)
						.sign (source.prv, source.pub)
						.work (*system.work.generate (latest))
						.build ();

			latest = send->hash ();

			auto open = builder
						.state ()
						.account (key.pub)
						.previous (0)
						.representative (key.pub)
						.balance (1)
						.link (send->hash ())
						.sign (key.prv, key.pub)
						.work (*system.work.generate (key.pub))
						.build ();

			sends.push_back (send);
			opens.push_back (open);
		}
	}

	// Initialize nodes with blocks without the `updates` frontiers
	std::vector<std::shared_ptr<celerix::block>> blocks;
	blocks.insert (blocks.end (), sends.begin (), sends.end ());
	system.set_initialization_blocks ({ blocks.begin (), blocks.end () });

	auto & node0 = *system.add_node (config, flags);
	ASSERT_TRUE (celerix::test::process (node0, opens));

	// No blocks should be broadcast to the other node
	auto & node1 = *system.add_node (config, flags);
	ASSERT_ALWAYS_EQ (100ms, node1.ledger.block_count (), blocks.size () + 1);

	// Frontier scan should detect all the accounts with missing blocks
	ASSERT_TIMELY (10s, std::all_of (opens.begin (), opens.end (), [&node1] (auto const & block) {
		return node1.bootstrap.prioritized (block->account ());
	}));
}

/*
 * Bootstrap should not attempt to prioritize accounts that can't be immediately connected to the ledger (no pending blocks, no existing frontier)
 */
TEST (bootstrap, frontier_scan_cannot_prioritize)
{
	celerix::test::system system;

	celerix::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	celerix::node_config config;
	// Disable other bootstrap strategies
	config.bootstrap.enable_scan = false;
	config.bootstrap.enable_dependency_walker = false;
	// Disable election activation
	config.backlog_scan.enable = false;
	config.priority_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;
	config.hinted_scheduler.enable = false;

	// Prepare blocks for frontier scan (genesis 10 sends -> 10 opens -> 10 sends -> 10 opens)
	std::vector<std::shared_ptr<celerix::block>> sends;
	std::vector<std::shared_ptr<celerix::block>> opens;
	std::vector<std::shared_ptr<celerix::block>> sends2;
	std::vector<std::shared_ptr<celerix::block>> opens2;
	{
		auto source = celerix::dev::genesis_key;
		auto latest = celerix::dev::genesis->hash ();
		auto balance = celerix::dev::genesis->balance ().number ();

		size_t const count = 10;

		for (int n = 0; n < count; ++n)
		{
			celerix::keypair key, key2;
			celerix::block_builder builder;

			balance -= 1;
			auto send = builder
						.state ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.balance (balance)
						.link (key.pub)
						.sign (source.prv, source.pub)
						.work (*system.work.generate (latest))
						.build ();

			latest = send->hash ();

			auto open = builder
						.state ()
						.account (key.pub)
						.previous (0)
						.representative (key.pub)
						.balance (1)
						.link (send->hash ())
						.sign (key.prv, key.pub)
						.work (*system.work.generate (key.pub))
						.build ();

			auto send2 = builder
						 .state ()
						 .account (key.pub)
						 .previous (open->hash ())
						 .representative (key.pub)
						 .balance (0)
						 .link (key2.pub)
						 .sign (key.prv, key.pub)
						 .work (*system.work.generate (open->hash ()))
						 .build ();

			auto open2 = builder
						 .state ()
						 .account (key2.pub)
						 .previous (0)
						 .representative (key2.pub)
						 .balance (1)
						 .link (send2->hash ())
						 .sign (key2.prv, key2.pub)
						 .work (*system.work.generate (key2.pub))
						 .build ();

			sends.push_back (send);
			opens.push_back (open);
			sends2.push_back (send2);
			opens2.push_back (open2);
		}
	}

	// Initialize nodes with blocks without the `updates` frontiers
	std::vector<std::shared_ptr<celerix::block>> blocks;
	blocks.insert (blocks.end (), sends.begin (), sends.end ());
	blocks.insert (blocks.end (), opens.begin (), opens.end ());
	system.set_initialization_blocks ({ blocks.begin (), blocks.end () });

	auto & node0 = *system.add_node (config, flags);
	ASSERT_TRUE (celerix::test::process (node0, sends2));
	ASSERT_TRUE (celerix::test::process (node0, opens2));

	// No blocks should be broadcast to the other node
	auto & node1 = *system.add_node (config, flags);
	ASSERT_ALWAYS_EQ (100ms, node1.ledger.block_count (), blocks.size () + 1);

	// Frontier scan should not detect the accounts
	ASSERT_ALWAYS (1s, std::none_of (opens2.begin (), opens2.end (), [&node1] (auto const & block) {
		return node1.bootstrap.prioritized (block->account ());
	}));
}