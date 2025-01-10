#include <celerix/lib/blocks.hpp>
#include <celerix/lib/thread_runner.hpp>
#include <celerix/lib/work_version.hpp>
#include <celerix/node/transport/inproc.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/test_common/network.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (system, work_generate_limited)
{
	celerix::test::system system;
	celerix::block_hash key (1);
	auto min = celerix::dev::network_params.work.entry;
	auto max = celerix::dev::network_params.work.base;
	for (int i = 0; i < 5; ++i)
	{
		auto work = system.work_generate_limited (key, min, max);
		auto difficulty = celerix::dev::network_params.work.difficulty (celerix::work_version::work_1, key, work);
		ASSERT_GE (difficulty, min);
		ASSERT_LT (difficulty, max);
	}
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
	celerix::test::system system (2);
	for (auto & i : system.nodes)
	{
		auto transaction = i->ledger.tx_begin_read ();
		ASSERT_EQ (celerix::dev::constants.genesis_amount, i->ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
	}
}

TEST (system, DISABLED_generate_send_existing)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::thread_runner runner (system.io_ctx, system.logger, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, stake_preserver.pub, celerix::dev::constants.genesis_amount / 3 * 2, true));
	auto info1 = node1.ledger.any.account_get (node1.ledger.tx_begin_read (), celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	std::vector<celerix::account> accounts;
	accounts.push_back (celerix::dev::genesis_key.pub);
	system.generate_send_existing (node1, accounts);
	// Have stake_preserver receive funds after generate_send_existing so it isn't chosen as the destination
	{
		auto transaction = node1.ledger.tx_begin_write ();
		celerix::block_builder builder;
		auto open_block = builder
						  .open ()
						  .source (send_block->hash ())
						  .representative (celerix::dev::genesis_key.pub)
						  .account (stake_preserver.pub)
						  .sign (stake_preserver.prv, stake_preserver.pub)
						  .work (0)
						  .build ();
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, open_block));
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (celerix::dev::genesis_key.pub));
	auto info2 = node1.ledger.any.account_get (node1.ledger.tx_begin_read (), celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info2);
	ASSERT_NE (info1->head, info2->head);
	system.deadline_set (15s);
	while (info2->block_count < info1->block_count + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction = node1.ledger.tx_begin_read ();
		info2 = node1.ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
		ASSERT_TRUE (info2);
	}
	ASSERT_EQ (info1->block_count + 2, info2->block_count);
	ASSERT_EQ (info2->balance, celerix::dev::constants.genesis_amount / 3);
	{
		ASSERT_NE (node1.ledger.any.block_amount (node1.ledger.tx_begin_read (), info2->head), 0);
	}
	system.stop ();
	runner.join ();
}

TEST (system, DISABLED_generate_send_new)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::thread_runner runner (system.io_ctx, system.logger, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	{
		auto transaction (node1.store.tx_begin_read ());
		auto iterator1 (node1.store.account.begin (transaction));
		ASSERT_NE (node1.store.account.end (transaction), iterator1);
		++iterator1;
		ASSERT_EQ (node1.store.account.end (transaction), iterator1);
	}
	celerix::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, stake_preserver.pub, celerix::dev::constants.genesis_amount / 3 * 2, true));
	{
		auto transaction = node1.ledger.tx_begin_write ();
		celerix::block_builder builder;
		auto open_block = builder
						  .open ()
						  .source (send_block->hash ())
						  .representative (celerix::dev::genesis_key.pub)
						  .account (stake_preserver.pub)
						  .sign (stake_preserver.prv, stake_preserver.pub)
						  .work (0)
						  .build ();
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, open_block));
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (celerix::dev::genesis_key.pub));
	std::vector<celerix::account> accounts;
	accounts.push_back (celerix::dev::genesis_key.pub);
	// This indirectly waits for online weight to stabilize, required to prevent intermittent failures
	ASSERT_TIMELY (5s, node1.wallets.reps ().voting > 0);
	system.generate_send_new (node1, accounts);
	celerix::account new_account{};
	{
		auto transaction (node1.wallets.tx_begin_read ());
		auto iterator2 (system.wallet (0)->store.begin (transaction));
		if (iterator2->first != celerix::dev::genesis_key.pub)
		{
			new_account = iterator2->first;
		}
		++iterator2;
		ASSERT_NE (system.wallet (0)->store.end (transaction), iterator2);
		if (iterator2->first != celerix::dev::genesis_key.pub)
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
	celerix::test::system system;
	celerix::keypair key;
	system.ledger_initialization_set ({ key });
	auto node = system.add_node ();
	ASSERT_EQ (celerix::dev::constants.genesis_amount, node->balance (key.pub));
}

TEST (system, rep_initialize_two)
{
	celerix::test::system system;
	celerix::keypair key0;
	celerix::keypair key1;
	system.ledger_initialization_set ({ key0, key1 });
	auto node = system.add_node ();
	ASSERT_EQ (celerix::dev::constants.genesis_amount / 2, node->balance (key0.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount / 2, node->balance (key1.pub));
}

TEST (system, rep_initialize_one_reserve)
{
	celerix::test::system system;
	celerix::keypair key;
	system.ledger_initialization_set ({ key }, celerix::Kcelerix_ratio);
	auto node = system.add_node ();
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, node->balance (key.pub));
	ASSERT_EQ (celerix::Kcelerix_ratio, node->balance (celerix::dev::genesis_key.pub));
}

TEST (system, rep_initialize_two_reserve)
{
	celerix::test::system system;
	celerix::keypair key0;
	celerix::keypair key1;
	system.ledger_initialization_set ({ key0, key1 }, celerix::Kcelerix_ratio);
	auto node = system.add_node ();
	ASSERT_EQ ((celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio) / 2, node->balance (key0.pub));
	ASSERT_EQ ((celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio) / 2, node->balance (key1.pub));
}

TEST (system, rep_initialize_many)
{
	celerix::test::system system;
	celerix::keypair key0;
	celerix::keypair key1;
	system.ledger_initialization_set ({ key0, key1 }, celerix::Kcelerix_ratio);
	auto node0 = system.add_node ();
	ASSERT_EQ ((celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio) / 2, node0->balance (key0.pub));
	ASSERT_EQ ((celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio) / 2, node0->balance (key1.pub));
	auto node1 = system.add_node ();
	ASSERT_EQ ((celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio) / 2, node1->balance (key0.pub));
	ASSERT_EQ ((celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio) / 2, node1->balance (key1.pub));
}

TEST (system, transport_basic)
{
	celerix::test::system system{ 1 };
	auto & node0 = *system.nodes[0];
	// Start nodes in separate systems so they don't automatically connect with each other.
	celerix::test::system system1{ 1 };
	auto & node1 = *system1.nodes[0];
	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::message, celerix::stat::detail::keepalive, celerix::stat::dir::in));
	celerix::transport::inproc::channel channel{ node0, node1 };
	// Send a keepalive message since they are easy to construct
	celerix::keepalive junk{ celerix::dev::network_params.network };
	channel.send (junk, celerix::transport::traffic_type::test);
	// Ensure the keepalive has been reecived on the target.
	ASSERT_TIMELY (5s, node1.stats.count (celerix::stat::type::message, celerix::stat::detail::keepalive, celerix::stat::dir::in) > 0);
}
