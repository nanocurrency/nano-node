#include <nano/lib/blocks.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (system, work_generate_limited)
{
	nano::test::system system;
	nano::block_hash key (1);
	auto min = nano::dev::network_params.work.entry;
	auto max = nano::dev::network_params.work.base;
	for (int i = 0; i < 5; ++i)
	{
		auto work = system.work_generate_limited (key, min, max);
		auto difficulty = nano::dev::network_params.work.difficulty (nano::work_version::work_1, key, work);
		ASSERT_GE (difficulty, min);
		ASSERT_LT (difficulty, max);
	}
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
	nano::test::system system (2);
	for (auto & i : system.nodes)
	{
		auto transaction = i->ledger.tx_begin_read ();
		ASSERT_EQ (nano::dev::constants.genesis_amount, i->ledger.any.account_balance (transaction, nano::dev::genesis_key.pub));
	}
}

TEST (system, DISABLED_generate_send_existing)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::thread_runner runner (system.io_ctx, system.logger, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (nano::dev::genesis_key.pub, stake_preserver.pub, nano::dev::constants.genesis_amount / 3 * 2, true));
	auto info1 = node1.ledger.any.account_get (node1.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	std::vector<nano::account> accounts;
	accounts.push_back (nano::dev::genesis_key.pub);
	system.generate_send_existing (node1, accounts);
	// Have stake_preserver receive funds after generate_send_existing so it isn't chosen as the destination
	{
		auto transaction = node1.ledger.tx_begin_write ();
		nano::block_builder builder;
		auto open_block = builder
						  .open ()
						  .source (send_block->hash ())
						  .representative (nano::dev::genesis_key.pub)
						  .account (stake_preserver.pub)
						  .sign (stake_preserver.prv, stake_preserver.pub)
						  .work (0)
						  .build ();
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (nano::block_status::progress, node1.ledger.process (transaction, open_block));
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (nano::dev::genesis_key.pub));
	auto info2 = node1.ledger.any.account_get (node1.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	ASSERT_TRUE (info2);
	ASSERT_NE (info1->head, info2->head);
	system.deadline_set (15s);
	while (info2->block_count < info1->block_count + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction = node1.ledger.tx_begin_read ();
		info2 = node1.ledger.any.account_get (transaction, nano::dev::genesis_key.pub);
		ASSERT_TRUE (info2);
	}
	ASSERT_EQ (info1->block_count + 2, info2->block_count);
	ASSERT_EQ (info2->balance, nano::dev::constants.genesis_amount / 3);
	{
		ASSERT_NE (node1.ledger.any.block_amount (node1.ledger.tx_begin_read (), info2->head), 0);
	}
	system.stop ();
	runner.join ();
}

TEST (system, DISABLED_generate_send_new)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::thread_runner runner (system.io_ctx, system.logger, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	{
		auto transaction (node1.store.tx_begin_read ());
		auto iterator1 (node1.store.account.begin (transaction));
		ASSERT_NE (node1.store.account.end (transaction), iterator1);
		++iterator1;
		ASSERT_EQ (node1.store.account.end (transaction), iterator1);
	}
	nano::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (nano::dev::genesis_key.pub, stake_preserver.pub, nano::dev::constants.genesis_amount / 3 * 2, true));
	{
		auto transaction = node1.ledger.tx_begin_write ();
		nano::block_builder builder;
		auto open_block = builder
						  .open ()
						  .source (send_block->hash ())
						  .representative (nano::dev::genesis_key.pub)
						  .account (stake_preserver.pub)
						  .sign (stake_preserver.prv, stake_preserver.pub)
						  .work (0)
						  .build ();
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (nano::block_status::progress, node1.ledger.process (transaction, open_block));
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (nano::dev::genesis_key.pub));
	std::vector<nano::account> accounts;
	accounts.push_back (nano::dev::genesis_key.pub);
	// This indirectly waits for online weight to stabilize, required to prevent intermittent failures
	ASSERT_TIMELY (5s, node1.wallets.reps ().voting > 0);
	system.generate_send_new (node1, accounts);
	nano::account new_account{};
	{
		auto transaction (node1.wallets.tx_begin_read ());
		auto iterator2 (system.wallet (0)->store.begin (transaction));
		if (iterator2->first != nano::dev::genesis_key.pub)
		{
			new_account = iterator2->first;
		}
		++iterator2;
		ASSERT_NE (system.wallet (0)->store.end (transaction), iterator2);
		if (iterator2->first != nano::dev::genesis_key.pub)
		{
			new_account = iterator2->first;
		}
		++iterator2;
		ASSERT_EQ (system.wallet (0)->store.end (transaction), iterator2);
		ASSERT_FALSE (new_account.is_zero ());
	}
	ASSERT_TIMELY (10s, node1.balance (new_account) != 0);
	system.stop ();
	runner.join ();
}

TEST (system, rep_initialize_one)
{
	nano::test::system system;
	nano::keypair key;
	system.ledger_initialization_set ({ key });
	auto node = system.add_node ();
	ASSERT_EQ (nano::dev::constants.genesis_amount, node->balance (key.pub));
}

TEST (system, rep_initialize_two)
{
	nano::test::system system;
	nano::keypair key0;
	nano::keypair key1;
	system.ledger_initialization_set ({ key0, key1 });
	auto node = system.add_node ();
	ASSERT_EQ (nano::dev::constants.genesis_amount / 2, node->balance (key0.pub));
	ASSERT_EQ (nano::dev::constants.genesis_amount / 2, node->balance (key1.pub));
}

TEST (system, rep_initialize_one_reserve)
{
	nano::test::system system;
	nano::keypair key;
	system.ledger_initialization_set ({ key }, nano::Knano_ratio);
	auto node = system.add_node ();
	ASSERT_EQ (nano::dev::constants.genesis_amount - nano::Knano_ratio, node->balance (key.pub));
	ASSERT_EQ (nano::Knano_ratio, node->balance (nano::dev::genesis_key.pub));
}

TEST (system, rep_initialize_two_reserve)
{
	nano::test::system system;
	nano::keypair key0;
	nano::keypair key1;
	system.ledger_initialization_set ({ key0, key1 }, nano::Knano_ratio);
	auto node = system.add_node ();
	ASSERT_EQ ((nano::dev::constants.genesis_amount - nano::Knano_ratio) / 2, node->balance (key0.pub));
	ASSERT_EQ ((nano::dev::constants.genesis_amount - nano::Knano_ratio) / 2, node->balance (key1.pub));
}

TEST (system, rep_initialize_many)
{
	nano::test::system system;
	nano::keypair key0;
	nano::keypair key1;
	system.ledger_initialization_set ({ key0, key1 }, nano::Knano_ratio);
	auto node0 = system.add_node ();
	ASSERT_EQ ((nano::dev::constants.genesis_amount - nano::Knano_ratio) / 2, node0->balance (key0.pub));
	ASSERT_EQ ((nano::dev::constants.genesis_amount - nano::Knano_ratio) / 2, node0->balance (key1.pub));
	auto node1 = system.add_node ();
	ASSERT_EQ ((nano::dev::constants.genesis_amount - nano::Knano_ratio) / 2, node1->balance (key0.pub));
	ASSERT_EQ ((nano::dev::constants.genesis_amount - nano::Knano_ratio) / 2, node1->balance (key1.pub));
}

TEST (system, transport_basic)
{
	nano::test::system system{ 1 };
	auto & node0 = *system.nodes[0];
	// Start nodes in separate systems so they don't automatically connect with each other.
	nano::test::system system1{ 1 };
	auto & node1 = *system1.nodes[0];
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in));
	nano::transport::inproc::channel channel{ node0, node1 };
	// Send a keepalive message since they are easy to construct
	nano::keepalive junk{ nano::dev::network_params.network };
	channel.send (junk);
	// Ensure the keepalive has been reecived on the target.
	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in) > 0);
}
