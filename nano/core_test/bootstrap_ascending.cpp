#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/bootstrap_ascending/database_scan.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/test_common/ledger_context.hpp>
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

TEST (bootstrap_ascending, pending_database_scanner)
{
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	// Prepare pending sends from genesis
	// 1 account with 1 pending
	// 1 account with 21 pendings
	// 2 accounts with 1 pending each
	std::deque<std::shared_ptr<nano::block>> blocks;
	nano::keypair key1, key2, key3, key4;
	{
		nano::state_block_builder builder;

		auto source = nano::dev::genesis_key;
		auto latest = nano::dev::genesis->hash ();
		auto balance = nano::dev::genesis->balance ().number ();

		// 1 account with 1 pending
		{
			auto send = builder.make_block ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.link (key1.pub)
						.balance (balance - 1)
						.sign (source.prv, source.pub)
						.work (*pool.generate (latest))
						.build ();
			latest = send->hash ();
			balance = send->balance_field ().value ().number ();
			blocks.push_back (send);
		}
		// 1 account with 21 pendings
		for (int i = 0; i < 21; ++i)
		{
			auto send = builder.make_block ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.link (key2.pub)
						.balance (balance - 1)
						.sign (source.prv, source.pub)
						.work (*pool.generate (latest))
						.build ();
			latest = send->hash ();
			balance = send->balance_field ().value ().number ();
			blocks.push_back (send);
		}
		// 2 accounts with 1 pending each
		{
			auto send = builder.make_block ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.link (key3.pub)
						.balance (balance - 1)
						.sign (source.prv, source.pub)
						.work (*pool.generate (latest))
						.build ();
			latest = send->hash ();
			balance = send->balance_field ().value ().number ();
			blocks.push_back (send);
		}
		{
			auto send = builder.make_block ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.link (key4.pub)
						.balance (balance - 1)
						.sign (source.prv, source.pub)
						.work (*pool.generate (latest))
						.build ();
			latest = send->hash ();
			balance = send->balance_field ().value ().number ();
			blocks.push_back (send);
		}
	}

	nano::test::ledger_context ctx{ std::move (blocks) };

	// Single batch
	{
		nano::bootstrap_ascending::pending_database_iterator scanner{ ctx.ledger () };
		auto transaction = ctx.store ().tx_begin_read ();
		auto accounts = scanner.next_batch (transaction, 256);

		// Check that account set contains all keys
		ASSERT_EQ (accounts.size (), 4);
		ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key1.pub) != accounts.end ());
		ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key2.pub) != accounts.end ());
		ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key3.pub) != accounts.end ());
		ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key4.pub) != accounts.end ());

		ASSERT_EQ (scanner.completed, 1);
	}
	// Multi batch
	{
		nano::bootstrap_ascending::pending_database_iterator scanner{ ctx.ledger () };
		auto transaction = ctx.store ().tx_begin_read ();

		// Request accounts in multiple batches
		auto accounts1 = scanner.next_batch (transaction, 2);
		auto accounts2 = scanner.next_batch (transaction, 1);
		auto accounts3 = scanner.next_batch (transaction, 1);

		ASSERT_EQ (accounts1.size (), 2);
		ASSERT_EQ (accounts2.size (), 1);
		ASSERT_EQ (accounts3.size (), 1);

		std::deque<nano::account> accounts;
		accounts.insert (accounts.end (), accounts1.begin (), accounts1.end ());
		accounts.insert (accounts.end (), accounts2.begin (), accounts2.end ());
		accounts.insert (accounts.end (), accounts3.begin (), accounts3.end ());

		// Check that account set contains all keys
		ASSERT_EQ (accounts.size (), 4);
		ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key1.pub) != accounts.end ());
		ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key2.pub) != accounts.end ());
		ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key3.pub) != accounts.end ());
		ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key4.pub) != accounts.end ());

		ASSERT_EQ (scanner.completed, 1);
	}
}

TEST (bootstrap_ascending, account_database_scanner)
{
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	size_t const count = 4;

	// Prepare some accounts
	std::deque<std::shared_ptr<nano::block>> blocks;
	std::deque<nano::keypair> keys;
	{
		nano::state_block_builder builder;

		auto source = nano::dev::genesis_key;
		auto latest = nano::dev::genesis->hash ();
		auto balance = nano::dev::genesis->balance ().number ();

		for (int i = 0; i < count; ++i)
		{
			nano::keypair key;
			auto send = builder.make_block ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.link (key.pub)
						.balance (balance - 1)
						.sign (source.prv, source.pub)
						.work (*pool.generate (latest))
						.build ();
			auto open = builder.make_block ()
						.account (key.pub)
						.previous (0)
						.representative (key.pub)
						.link (send->hash ())
						.balance (1)
						.sign (key.prv, key.pub)
						.work (*pool.generate (key.pub))
						.build ();
			latest = send->hash ();
			balance = send->balance_field ().value ().number ();
			blocks.push_back (send);
			blocks.push_back (open);
			keys.push_back (key);
		}
	}

	nano::test::ledger_context ctx{ std::move (blocks) };

	// Single batch
	{
		nano::bootstrap_ascending::account_database_iterator scanner{ ctx.ledger () };
		auto transaction = ctx.store ().tx_begin_read ();
		auto accounts = scanner.next_batch (transaction, 256);

		// Check that account set contains all keys
		ASSERT_EQ (accounts.size (), keys.size () + 1); // +1 for genesis
		for (auto const & key : keys)
		{
			ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key.pub) != accounts.end ());
		}
		ASSERT_EQ (scanner.completed, 1);
	}
	// Multi batch
	{
		nano::bootstrap_ascending::account_database_iterator scanner{ ctx.ledger () };
		auto transaction = ctx.store ().tx_begin_read ();

		// Request accounts in multiple batches
		auto accounts1 = scanner.next_batch (transaction, 2);
		auto accounts2 = scanner.next_batch (transaction, 2);
		auto accounts3 = scanner.next_batch (transaction, 1);

		ASSERT_EQ (accounts1.size (), 2);
		ASSERT_EQ (accounts2.size (), 2);
		ASSERT_EQ (accounts3.size (), 1);

		std::deque<nano::account> accounts;
		accounts.insert (accounts.end (), accounts1.begin (), accounts1.end ());
		accounts.insert (accounts.end (), accounts2.begin (), accounts2.end ());
		accounts.insert (accounts.end (), accounts3.begin (), accounts3.end ());

		// Check that account set contains all keys
		ASSERT_EQ (accounts.size (), keys.size () + 1); // +1 for genesis
		for (auto const & key : keys)
		{
			ASSERT_TRUE (std::find (accounts.begin (), accounts.end (), key.pub) != accounts.end ());
		}
		ASSERT_EQ (scanner.completed, 1);
	}
}