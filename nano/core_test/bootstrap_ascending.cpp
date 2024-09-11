#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <sstream>

using namespace std::chrono_literals;

namespace
{
nano::block_hash random_hash ()
{
	nano::block_hash random_hash;
	nano::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	return random_hash;
}
}

TEST (account_sets, construction)
{
	nano::test::system system;
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
}

TEST (account_sets, empty_blocked)
{
	nano::test::system system;

	nano::account account{ 1 };
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
	ASSERT_FALSE (sets.blocked (account));
}

TEST (account_sets, block)
{
	nano::test::system system;

	nano::account account{ 1 };
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
	sets.block (account, random_hash ());
	ASSERT_TRUE (sets.blocked (account));
}

TEST (account_sets, unblock)
{
	nano::test::system system;

	nano::account account{ 1 };
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
	auto hash = random_hash ();
	sets.block (account, hash);
	sets.unblock (account, hash);
	ASSERT_FALSE (sets.blocked (account));
}

TEST (account_sets, priority_base)
{
	nano::test::system system;

	nano::account account{ 1 };
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
	ASSERT_EQ (0.0, sets.priority (account));
}

TEST (account_sets, priority_blocked)
{
	nano::test::system system;

	nano::account account{ 1 };
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
	sets.block (account, random_hash ());
	ASSERT_EQ (0.0, sets.priority (account));
}

// When account is unblocked, check that it retains it former priority
TEST (account_sets, priority_unblock_keep)
{
	nano::test::system system;

	nano::account account{ 1 };
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
	sets.priority_up (account);
	sets.priority_up (account);
	ASSERT_EQ (sets.priority (account), nano::bootstrap_ascending::account_sets::priority_initial + nano::bootstrap_ascending::account_sets::priority_increase);
	auto hash = random_hash ();
	sets.block (account, hash);
	ASSERT_EQ (0.0, sets.priority (account));
	sets.unblock (account, hash);
	ASSERT_EQ (sets.priority (account), nano::bootstrap_ascending::account_sets::priority_initial + nano::bootstrap_ascending::account_sets::priority_increase);
}

TEST (account_sets, priority_up_down)
{
	nano::test::system system;

	nano::account account{ 1 };
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
	sets.priority_up (account);
	ASSERT_EQ (sets.priority (account), nano::bootstrap_ascending::account_sets::priority_initial);
	sets.priority_down (account);
	ASSERT_EQ (sets.priority (account), nano::bootstrap_ascending::account_sets::priority_initial / nano::bootstrap_ascending::account_sets::priority_divide);
}

TEST (account_sets, priority_down_sat)
{
	nano::test::system system;

	nano::account account{ 1 };
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
	sets.priority_down (account);
	ASSERT_EQ (0.0, sets.priority (account));
}

// Ensure priority value is bounded
TEST (account_sets, saturate_priority)
{
	nano::test::system system;

	nano::account account{ 1 };
	auto store = nano::make_store (system.logger, nano::unique_path (), nano::dev::constants);
	ASSERT_FALSE (store->init_error ());
	nano::account_sets_config config;
	nano::bootstrap_ascending::account_sets sets{ config, system.stats };
	for (int n = 0; n < 1000; ++n)
	{
		sets.priority_up (account);
	}
	ASSERT_EQ (sets.priority (account), nano::bootstrap_ascending::account_sets::priority_max);
}

/**
 * Tests the base case for returning
 */
TEST (bootstrap_ascending, account_base)
{
	nano::node_flags flags;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node0.process (send1));
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()) != nullptr);
}

/**
 * Tests that bootstrap_ascending will return multiple new blocks in-order
 */
TEST (bootstrap_ascending, account_inductive)
{
	nano::node_flags flags;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	//	std::cerr << "Genesis: " << nano::dev::genesis->hash ().to_string () << std::endl;
	//	std::cerr << "Send1: " << send1->hash ().to_string () << std::endl;
	//	std::cerr << "Send2: " << send2->hash ().to_string () << std::endl;
	ASSERT_EQ (nano::block_status::progress, node0.process (send1));
	ASSERT_EQ (nano::block_status::progress, node0.process (send2));
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (50s, node1.block (send2->hash ()) != nullptr);
}

/**
 * Tests that bootstrap_ascending will return multiple new blocks in-order
 */
TEST (bootstrap_ascending, trace_base)
{
	nano::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::keypair key;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto receive1 = builder.make_block ()
					.account (key.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.link (send1->hash ())
					.balance (1)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build ();
	//	std::cerr << "Genesis key: " << nano::dev::genesis_key.pub.to_account () << std::endl;
	//	std::cerr << "Key: " << key.pub.to_account () << std::endl;
	//	std::cerr << "Genesis: " << nano::dev::genesis->hash ().to_string () << std::endl;
	//	std::cerr << "send1: " << send1->hash ().to_string () << std::endl;
	//	std::cerr << "receive1: " << receive1->hash ().to_string () << std::endl;
	auto & node1 = *system.add_node ();
	//	std::cerr << "--------------- Start ---------------\n";
	ASSERT_EQ (nano::block_status::progress, node0.process (send1));
	ASSERT_EQ (nano::block_status::progress, node0.process (receive1));
	ASSERT_EQ (node1.ledger.any.receivable_end (), node1.ledger.any.receivable_upper_bound (node1.ledger.tx_begin_read (), key.pub, 0));
	//	std::cerr << "node0: " << node0.network.endpoint () << std::endl;
	//	std::cerr << "node1: " << node1.network.endpoint () << std::endl;
	ASSERT_TIMELY (10s, node1.block (receive1->hash ()) != nullptr);
}
