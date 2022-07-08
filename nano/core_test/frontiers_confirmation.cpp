#include <nano/node/active_transactions.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace nano
{
TEST (frontiers_confirmation, prioritize_frontiers)
{
	nano::system system;
	// Prevent frontiers being confirmed as it will affect the priorization checking
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);

	nano::keypair key1;
	nano::keypair key2;
	nano::keypair key3;
	nano::keypair key4;
	nano::block_builder builder;
	nano::block_hash latest1 (node->latest (nano::dev::genesis_key.pub));

	// Send different numbers of blocks all accounts
	auto send1 = builder
				 .send ()
				 .previous (latest1)
				 .destination (key1.pub)
				 .balance (node->config.online_weight_minimum.number () + 10000)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest1))
				 .build ();
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (node->config.online_weight_minimum.number () + 8500)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto send3 = builder
				 .send ()
				 .previous (send2->hash ())
				 .destination (key1.pub)
				 .balance (node->config.online_weight_minimum.number () + 8000)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	auto send4 = builder
				 .send ()
				 .previous (send3->hash ())
				 .destination (key2.pub)
				 .balance (node->config.online_weight_minimum.number () + 7500)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send3->hash ()))
				 .build ();
	auto send5 = builder
				 .send ()
				 .previous (send4->hash ())
				 .destination (key3.pub)
				 .balance (node->config.online_weight_minimum.number () + 6500)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send4->hash ()))
				 .build ();
	auto send6 = builder
				 .send ()
				 .previous (send5->hash ())
				 .destination (key4.pub)
				 .balance (node->config.online_weight_minimum.number () + 6000)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send5->hash ()))
				 .build ();

	// Open all accounts and add other sends to get different uncemented counts (as well as some which are the same)
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (nano::dev::genesis->account ())
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	auto send7 = builder
				 .send ()
				 .previous (open1->hash ())
				 .destination (nano::dev::genesis_key.pub)
				 .balance (500)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (open1->hash ()))
				 .build ();

	auto open2 = builder
				 .open ()
				 .source (send4->hash ())
				 .representative (nano::dev::genesis->account ())
				 .account (key2.pub)
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (key2.pub))
				 .build ();

	auto open3 = builder
				 .open ()
				 .source (send5->hash ())
				 .representative (nano::dev::genesis->account ())
				 .account (key3.pub)
				 .sign (key3.prv, key3.pub)
				 .work (*system.work.generate (key3.pub))
				 .build ();
	auto send8 = builder
				 .send ()
				 .previous (open3->hash ())
				 .destination (nano::dev::genesis_key.pub)
				 .balance (500)
				 .sign (key3.prv, key3.pub)
				 .work (*system.work.generate (open3->hash ()))
				 .build ();
	auto send9 = builder
				 .send ()
				 .previous (send8->hash ())
				 .destination (nano::dev::genesis_key.pub)
				 .balance (200)
				 .sign (key3.prv, key3.pub)
				 .work (*system.work.generate (send8->hash ()))
				 .build ();

	auto open4 = builder
				 .open ()
				 .source (send6->hash ())
				 .representative (nano::dev::genesis->account ())
				 .account (key4.pub)
				 .sign (key4.prv, key4.pub)
				 .work (*system.work.generate (key4.pub))
				 .build ();
	auto send10 = builder
				  .send ()
				  .previous (open4->hash ())
				  .destination (nano::dev::genesis_key.pub)
				  .balance (500)
				  .sign (key4.prv, key4.pub)
				  .work (*system.work.generate (open4->hash ()))
				  .build ();
	auto send11 = builder
				  .send ()
				  .previous (send10->hash ())
				  .destination (nano::dev::genesis_key.pub)
				  .balance (200)
				  .sign (key4.prv, key4.pub)
				  .work (*system.work.generate (send10->hash ()))
				  .build ();

	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send1).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send2).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send3).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send4).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send5).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send6).code);

		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open1).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send7).code);

		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open2).code);

		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open3).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send8).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send9).code);

		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open4).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send10).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send11).code);
	}

	auto transaction = node->store.tx_begin_read ();
	constexpr auto num_accounts = 5;
	auto priority_orders_match = [] (auto const & cementable_frontiers, auto const & desired_order) {
		return std::equal (desired_order.begin (), desired_order.end (), cementable_frontiers.template get<1> ().begin (), cementable_frontiers.template get<1> ().end (), [] (nano::account const & account, nano::cementable_account const & cementable_account) {
			return (account == cementable_account.account);
		});
	};
	{
		node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (1), std::chrono::seconds (1));
		ASSERT_EQ (node->active.priority_cementable_frontiers_size (), num_accounts);
		// Check the order of accounts is as expected (greatest number of uncemented blocks at the front). key3 and key4 have the same value, the order is unspecified so check both
		std::array<nano::account, num_accounts> desired_order_1{ nano::dev::genesis->account (), key3.pub, key4.pub, key1.pub, key2.pub };
		std::array<nano::account, num_accounts> desired_order_2{ nano::dev::genesis->account (), key4.pub, key3.pub, key1.pub, key2.pub };
		ASSERT_TRUE (priority_orders_match (node->active.priority_cementable_frontiers, desired_order_1) || priority_orders_match (node->active.priority_cementable_frontiers, desired_order_2));
	}

	{
		// Add some to the local node wallets and check ordering of both containers
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		system.wallet (0)->insert_adhoc (key1.prv);
		system.wallet (0)->insert_adhoc (key2.prv);
		node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (1), std::chrono::seconds (1));
		ASSERT_EQ (node->active.priority_cementable_frontiers_size (), num_accounts - 3);
		ASSERT_EQ (node->active.priority_wallet_cementable_frontiers_size (), num_accounts - 2);
		std::array<nano::account, 3> local_desired_order{ nano::dev::genesis->account (), key1.pub, key2.pub };
		ASSERT_TRUE (priority_orders_match (node->active.priority_wallet_cementable_frontiers, local_desired_order));
		std::array<nano::account, 2> desired_order_1{ key3.pub, key4.pub };
		std::array<nano::account, 2> desired_order_2{ key4.pub, key3.pub };
		ASSERT_TRUE (priority_orders_match (node->active.priority_cementable_frontiers, desired_order_1) || priority_orders_match (node->active.priority_cementable_frontiers, desired_order_2));
	}

	{
		// Add the remainder of accounts to node wallets and check size/ordering is correct
		system.wallet (0)->insert_adhoc (key3.prv);
		system.wallet (0)->insert_adhoc (key4.prv);
		node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (1), std::chrono::seconds (1));
		ASSERT_EQ (node->active.priority_cementable_frontiers_size (), 0);
		ASSERT_EQ (node->active.priority_wallet_cementable_frontiers_size (), num_accounts);
		std::array<nano::account, num_accounts> desired_order_1{ nano::dev::genesis->account (), key3.pub, key4.pub, key1.pub, key2.pub };
		std::array<nano::account, num_accounts> desired_order_2{ nano::dev::genesis->account (), key4.pub, key3.pub, key1.pub, key2.pub };
		ASSERT_TRUE (priority_orders_match (node->active.priority_wallet_cementable_frontiers, desired_order_1) || priority_orders_match (node->active.priority_wallet_cementable_frontiers, desired_order_2));
	}

	// Check that accounts which already exist have their order modified when the uncemented count changes.
	auto send12 = builder
				  .send ()
				  .previous (send9->hash ())
				  .destination (nano::dev::genesis_key.pub)
				  .balance (100)
				  .sign (key3.prv, key3.pub)
				  .work (*system.work.generate (send9->hash ()))
				  .build ();
	auto send13 = builder
				  .send ()
				  .previous (send12->hash ())
				  .destination (nano::dev::genesis_key.pub)
				  .balance (90)
				  .sign (key3.prv, key3.pub)
				  .work (*system.work.generate (send12->hash ()))
				  .build ();
	auto send14 = builder
				  .send ()
				  .previous (send13->hash ())
				  .destination (nano::dev::genesis_key.pub)
				  .balance (80)
				  .sign (key3.prv, key3.pub)
				  .work (*system.work.generate (send13->hash ()))
				  .build ();
	auto send15 = builder
				  .send ()
				  .previous (send14->hash ())
				  .destination (nano::dev::genesis_key.pub)
				  .balance (70)
				  .sign (key3.prv, key3.pub)
				  .work (*system.work.generate (send14->hash ()))
				  .build ();
	auto send16 = builder
				  .send ()
				  .previous (send15->hash ())
				  .destination (nano::dev::genesis_key.pub)
				  .balance (60)
				  .sign (key3.prv, key3.pub)
				  .work (*system.work.generate (send15->hash ()))
				  .build ();
	auto send17 = builder
				  .send ()
				  .previous (send16->hash ())
				  .destination (nano::dev::genesis_key.pub)
				  .balance (50)
				  .sign (key3.prv, key3.pub)
				  .work (*system.work.generate (send16->hash ()))
				  .build ();
	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send12).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send13).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send14).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send15).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send16).code);
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send17).code);
	}
	transaction.refresh ();
	node->active.prioritize_frontiers_for_confirmation (transaction, std::chrono::seconds (1), std::chrono::seconds (1));
	ASSERT_TRUE (priority_orders_match (node->active.priority_wallet_cementable_frontiers, std::array<nano::account, num_accounts>{ key3.pub, nano::dev::genesis->account (), key4.pub, key1.pub, key2.pub }));
	uint64_t election_count = 0;
	node->active.confirm_prioritized_frontiers (transaction, 100, election_count);

	// Check that the active transactions roots contains the frontiers
	ASSERT_TIMELY (10s, node->active.size () == num_accounts);

	std::array<nano::qualified_root, num_accounts> frontiers{ send17->qualified_root (), send6->qualified_root (), send7->qualified_root (), open2->qualified_root (), send11->qualified_root () };
	for (auto & frontier : frontiers)
	{
		ASSERT_TRUE (node->active.active (frontier));
	}
}

TEST (frontiers_confirmation, prioritize_frontiers_max_optimistic_elections)
{
	nano::system system;
	// Prevent frontiers being confirmed as it will affect the priorization checking
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);

	node->ledger.cache.cemented_count = node->ledger.bootstrap_weight_max_blocks - 1;
	auto max_optimistic_election_count_under_hardcoded_weight = node->active.max_optimistic ();
	node->ledger.cache.cemented_count = node->ledger.bootstrap_weight_max_blocks;
	auto max_optimistic_election_count = node->active.max_optimistic ();
	ASSERT_GT (max_optimistic_election_count_under_hardcoded_weight, max_optimistic_election_count);

	for (auto i = 0; i < max_optimistic_election_count * 2; ++i)
	{
		auto transaction = node->store.tx_begin_write ();
		auto latest = node->latest (nano::dev::genesis->account ());
		nano::keypair key;
		nano::block_builder builder;
		auto send = builder
					.send ()
					.previous (latest)
					.destination (key.pub)
					.balance (node->config.online_weight_minimum.number () + 10000)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
		auto open = builder
					.open ()
					.source (send->hash ())
					.representative (nano::dev::genesis->account ())
					.account (key.pub)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build ();
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open).code);
	}

	{
		nano::unique_lock<nano::mutex> lk (node->active.mutex);
		node->active.frontiers_confirmation (lk);
	}

	ASSERT_EQ (max_optimistic_election_count, node->active.roots.size ());

	nano::account next_frontier_account{ 2 };
	node->active.next_frontier_account = next_frontier_account;

	// Call frontiers confirmation again and confirm that next_frontier_account hasn't changed
	{
		nano::unique_lock<nano::mutex> lk (node->active.mutex);
		node->active.frontiers_confirmation (lk);
	}

	ASSERT_EQ (max_optimistic_election_count, node->active.roots.size ());
	ASSERT_EQ (next_frontier_account, node->active.next_frontier_account);
}

TEST (frontiers_confirmation, expired_optimistic_elections_removal)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);

	// This should be removed on the next prioritization call
	node->active.expired_optimistic_election_infos.emplace (std::chrono::steady_clock::now () - (node->active.expired_optimistic_election_info_cutoff + 1min), nano::account (1));
	ASSERT_EQ (1, node->active.expired_optimistic_election_infos.size ());
	node->active.prioritize_frontiers_for_confirmation (node->store.tx_begin_read (), 0s, 0s);
	ASSERT_EQ (0, node->active.expired_optimistic_election_infos.size ());

	// This should not be removed on the next prioritization call
	node->active.expired_optimistic_election_infos.emplace (std::chrono::steady_clock::now () - (node->active.expired_optimistic_election_info_cutoff - 1min), nano::account (1));
	ASSERT_EQ (1, node->active.expired_optimistic_election_infos.size ());
	node->active.prioritize_frontiers_for_confirmation (node->store.tx_begin_read (), 0s, 0s);
	ASSERT_EQ (1, node->active.expired_optimistic_election_infos.size ());
}
}

TEST (frontiers_confirmation, mode)
{
	nano::keypair key;
	nano::block_builder builder;
	nano::node_flags node_flags;
	// Always mode
	{
		nano::system system;
		nano::node_config node_config (nano::get_available_port (), system.logging);
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::always;
		auto node = system.add_node (node_config, node_flags);
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (nano::dev::genesis->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*node->work_generate_blocking (nano::dev::genesis->hash ()))
					.build ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
		}
		ASSERT_TIMELY (5s, node->active.size () == 1);
	}
	// Auto mode
	{
		nano::system system;
		nano::node_config node_config (nano::get_available_port (), system.logging);
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::automatic;
		auto node = system.add_node (node_config, node_flags);
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (nano::dev::genesis->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*node->work_generate_blocking (nano::dev::genesis->hash ()))
					.build ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
		}
		ASSERT_TIMELY (5s, node->active.size () == 1);
	}
	// Disabled mode
	{
		nano::system system;
		nano::node_config node_config (nano::get_available_port (), system.logging);
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (nano::dev::genesis->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*node->work_generate_blocking (nano::dev::genesis->hash ()))
					.build ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
		}
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		std::this_thread::sleep_for (std::chrono::seconds (1));
		ASSERT_EQ (0, node->active.size ());
	}
}
