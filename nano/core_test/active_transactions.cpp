#include <nano/core_test/testutil.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/node/election.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (active_transactions, confirm_one)
{
	nano::system system (1);
	auto & node1 = *system.nodes[0];
	// Send and vote for a block before peering with node2
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto send (system.wallet (0)->send_action (nano::test_genesis_key.pub, nano::public_key (), node1.config.receive_minimum.number ()));
	system.deadline_set (5s);
	while (!node1.active.empty () && !node1.block_confirmed_or_being_confirmed (node1.store.tx_begin_read (), send->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto & node2 = *system.add_node (nano::node_config (nano::get_available_port (), system.logging));
	system.deadline_set (5s);
	// Let node2 know about the block
	while (node2.active.empty ())
	{
		node1.network.flood_block (send, false);
		ASSERT_NO_ERROR (system.poll ());
	}
	while (node2.ledger.cache.cemented_count < 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (active_transactions, adjusted_difficulty_priority)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::genesis genesis;
	nano::keypair key1, key2, key3;

	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 10 * nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 20 * nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send1->hash ())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10 * nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, *system.work.generate (key1.pub)));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10 * nano::xrb_ratio, send2->hash (), key2.prv, key2.pub, *system.work.generate (key2.pub)));
	node1.process_active (send1); // genesis
	node1.process_active (send2); // genesis
	node1.process_active (open1); // key1
	node1.process_active (open2); // key2
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Check adjusted difficulty
	{
		nano::lock_guard<std::mutex> active_guard (node1.active.mutex);
		ASSERT_EQ (node1.active.roots.get<1> ().begin ()->election->status.winner->hash (), send1->hash ());
		ASSERT_LT (node1.active.roots.find (send2->qualified_root ())->adjusted_difficulty, node1.active.roots.find (send1->qualified_root ())->adjusted_difficulty);
		ASSERT_LT (node1.active.roots.find (open1->qualified_root ())->adjusted_difficulty, node1.active.roots.find (send1->qualified_root ())->adjusted_difficulty);
		ASSERT_LT (node1.active.roots.find (open2->qualified_root ())->adjusted_difficulty, node1.active.roots.find (send2->qualified_root ())->adjusted_difficulty);
	}

	// Confirm elections
	system.deadline_set (10s);
	while (!node1.active.empty ())
	{
		nano::lock_guard<std::mutex> active_guard (node1.active.mutex);
		if (!node1.active.roots.empty ())
		{
			node1.active.roots.begin ()->election->confirm_once ();
		}
	}
	system.deadline_set (10s);
	while (node1.ledger.cache.cemented_count < 5)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	//genesis and key1,key2 are opened
	//start chain of 2 on each
	auto send3 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send2->hash (), nano::test_genesis_key.pub, 9 * nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send2->hash (), nano::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send4 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send3->hash (), nano::test_genesis_key.pub, 8 * nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send3->hash (), nano::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send5 (std::make_shared<nano::state_block> (key1.pub, open1->hash (), key1.pub, 9 * nano::xrb_ratio, key3.pub, key1.prv, key1.pub, *system.work.generate (open1->hash (), nano::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send6 (std::make_shared<nano::state_block> (key1.pub, send5->hash (), key1.pub, 8 * nano::xrb_ratio, key3.pub, key1.prv, key1.pub, *system.work.generate (send5->hash (), nano::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send7 (std::make_shared<nano::state_block> (key2.pub, open2->hash (), key2.pub, 9 * nano::xrb_ratio, key3.pub, key2.prv, key2.pub, *system.work.generate (open2->hash (), nano::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));
	auto send8 (std::make_shared<nano::state_block> (key2.pub, send7->hash (), key2.pub, 8 * nano::xrb_ratio, key3.pub, key2.prv, key2.pub, *system.work.generate (send7->hash (), nano::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));

	node1.process_active (send3); // genesis
	node1.process_active (send5); // key1
	node1.process_active (send7); // key2
	node1.process_active (send4); // genesis
	node1.process_active (send6); // key1
	node1.process_active (send8); // key2

	system.deadline_set (10s);
	while (node1.active.size () != 6)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Check adjusted difficulty
	nano::lock_guard<std::mutex> lock (node1.active.mutex);
	uint64_t last_adjusted (0);
	for (auto i (node1.active.roots.get<1> ().begin ()), n (node1.active.roots.get<1> ().end ()); i != n; ++i)
	{
		//first root has nothing to compare
		if (last_adjusted != 0)
		{
			ASSERT_LT (i->adjusted_difficulty, last_adjusted);
		}
		last_adjusted = i->adjusted_difficulty;
	}
	ASSERT_LT (node1.active.roots.find (send4->qualified_root ())->adjusted_difficulty, node1.active.roots.find (send3->qualified_root ())->adjusted_difficulty);
	ASSERT_LT (node1.active.roots.find (send6->qualified_root ())->adjusted_difficulty, node1.active.roots.find (send5->qualified_root ())->adjusted_difficulty);
	ASSERT_LT (node1.active.roots.find (send8->qualified_root ())->adjusted_difficulty, node1.active.roots.find (send7->qualified_root ())->adjusted_difficulty);
}

TEST (active_transactions, adjusted_difficulty_overflow_max)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::genesis genesis;
	nano::keypair key1, key2;

	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 10 * nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 20 * nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send1->hash ())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10 * nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, *system.work.generate (key1.pub)));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10 * nano::xrb_ratio, send2->hash (), key2.prv, key2.pub, *system.work.generate (key2.pub)));
	node1.process_active (send1); // genesis
	node1.process_active (send2); // genesis
	node1.process_active (open1); // key1
	node1.process_active (open2); // key2
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	{
		nano::lock_guard<std::mutex> active_guard (node1.active.mutex);
		// Update difficulty to maximum
		auto send1_root (node1.active.roots.find (send1->qualified_root ()));
		auto send2_root (node1.active.roots.find (send2->qualified_root ()));
		auto open1_root (node1.active.roots.find (open1->qualified_root ()));
		auto open2_root (node1.active.roots.find (open2->qualified_root ()));
		// clang-format off
		auto modify_difficulty = [& roots = node1.active.roots](auto & existing_root) {
			roots.modify (existing_root, [](nano::conflict_info & info_a) {
				info_a.difficulty = std::numeric_limits<std::uint64_t>::max ();
			});
		};
		// clang-format on
		modify_difficulty (send1_root);
		modify_difficulty (send2_root);
		modify_difficulty (open1_root);
		modify_difficulty (open2_root);
		node1.active.adjust_difficulty (send2->hash ());
		// Test overflow
		ASSERT_EQ (node1.active.roots.get<1> ().begin ()->election->status.winner->hash (), send1->hash ());
		ASSERT_EQ (send1_root->adjusted_difficulty, std::numeric_limits<std::uint64_t>::max ());
		ASSERT_LT (send2_root->adjusted_difficulty, send1_root->adjusted_difficulty);
		ASSERT_LT (open1_root->adjusted_difficulty, send1_root->adjusted_difficulty);
		ASSERT_LT (open2_root->adjusted_difficulty, send2_root->adjusted_difficulty);
	}
}

TEST (active_transactions, adjusted_difficulty_overflow_min)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::genesis genesis;
	nano::keypair key1, key2, key3;

	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 10 * nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 20 * nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send1->hash ())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10 * nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, *system.work.generate (key1.pub)));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10 * nano::xrb_ratio, send2->hash (), key2.prv, key2.pub, *system.work.generate (key2.pub)));
	auto send3 (std::make_shared<nano::state_block> (key2.pub, open2->hash (), key2.pub, 9 * nano::xrb_ratio, key3.pub, key2.prv, key2.pub, *system.work.generate (open2->hash ())));
	node1.process_active (send1); // genesis
	node1.process_active (send2); // genesis
	node1.process_active (open1); // key1
	node1.process_active (open2); // key2
	node1.process_active (send3); // key2
	system.deadline_set (10s);
	while (node1.active.size () != 5)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	{
		nano::lock_guard<std::mutex> active_guard (node1.active.mutex);
		// Update difficulty to minimum
		auto send1_root (node1.active.roots.find (send1->qualified_root ()));
		auto send2_root (node1.active.roots.find (send2->qualified_root ()));
		auto open1_root (node1.active.roots.find (open1->qualified_root ()));
		auto open2_root (node1.active.roots.find (open2->qualified_root ()));
		auto send3_root (node1.active.roots.find (send3->qualified_root ()));
		// clang-format off
		auto modify_difficulty = [& roots = node1.active.roots](auto & existing_root) {
			roots.modify (existing_root, [](nano::conflict_info & info_a) {
				info_a.difficulty = std::numeric_limits<std::uint64_t>::min () + 1;
			});
		};
		// clang-format on
		modify_difficulty (send1_root);
		modify_difficulty (send2_root);
		modify_difficulty (open1_root);
		modify_difficulty (open2_root);
		modify_difficulty (send3_root);
		node1.active.adjust_difficulty (send1->hash ());
		// Test overflow
		ASSERT_EQ (node1.active.roots.get<1> ().begin ()->election->status.winner->hash (), send1->hash ());
		ASSERT_EQ (send1_root->adjusted_difficulty, std::numeric_limits<std::uint64_t>::min () + 3);
		ASSERT_LT (send2_root->adjusted_difficulty, send1_root->adjusted_difficulty);
		ASSERT_LT (open1_root->adjusted_difficulty, send1_root->adjusted_difficulty);
		ASSERT_LT (open2_root->adjusted_difficulty, send2_root->adjusted_difficulty);
		ASSERT_LT (send3_root->adjusted_difficulty, open2_root->adjusted_difficulty);
		ASSERT_EQ (send3_root->adjusted_difficulty, std::numeric_limits<std::uint64_t>::min ());
		// Clear roots with too low difficulty to prevent issues
		node1.active.roots.clear ();
	}
}

TEST (active_transactions, keep_local)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.active_elections_size = 2; //bound to 2, wont drop wallet created transactions, but good to test dropping remote
	// Disable frontier confirmation to allow the test to finish before
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	nano::genesis genesis;
	//key 1/2 will be managed by the wallet
	nano::keypair key1, key2, key3, key4, key5, key6;
	wallet.insert_adhoc (nano::test_genesis_key.prv);
	auto send1 (wallet.send_action (nano::test_genesis_key.pub, key1.pub, node.config.receive_minimum.number ()));
	auto send2 (wallet.send_action (nano::test_genesis_key.pub, key2.pub, node.config.receive_minimum.number ()));
	auto send3 (wallet.send_action (nano::test_genesis_key.pub, key3.pub, node.config.receive_minimum.number ()));
	auto send4 (wallet.send_action (nano::test_genesis_key.pub, key4.pub, node.config.receive_minimum.number ()));
	auto send5 (wallet.send_action (nano::test_genesis_key.pub, key5.pub, node.config.receive_minimum.number ()));
	auto send6 (wallet.send_action (nano::test_genesis_key.pub, key6.pub, node.config.receive_minimum.number ()));
	system.deadline_set (10s);
	// should not drop wallet created transactions
	while (node.active.size () != 6)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, node.active.dropped_elections_cache_size ());
	while (!node.active.empty ())
	{
		nano::lock_guard<std::mutex> active_guard (node.active.mutex);
		if (!node.active.roots.empty ())
		{
			node.active.roots.begin ()->election->confirm_once ();
		}
	}
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, node.config.receive_minimum.number (), send1->hash (), key1.prv, key1.pub, *system.work.generate (key1.pub)));
	node.process_active (open1);
	node.active.start (open1);
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, node.config.receive_minimum.number (), send2->hash (), key2.prv, key2.pub, *system.work.generate (key2.pub)));
	node.process_active (open2);
	node.active.start (open2);
	auto open3 (std::make_shared<nano::state_block> (key3.pub, 0, key3.pub, node.config.receive_minimum.number (), send3->hash (), key3.prv, key3.pub, *system.work.generate (key3.pub)));
	node.process_active (open3);
	node.active.start (open3);
	ASSERT_EQ (3, node.active.size ());
	system.deadline_set (10s);
	// bound elections, should drop after one loop
	while (node.active.size () != node_config.active_elections_size)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, node.active.dropped_elections_cache_size ());
}

TEST (active_transactions, prioritize_chains)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.active_elections_size = 4; //bound to 4, wont drop wallet created transactions, but good to test dropping remote
	// Disable frontier confirmation to allow the test to finish before
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::genesis genesis;
	nano::keypair key1, key2, key3;

	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 10 * nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10 * nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, *system.work.generate (key1.pub)));
	auto send2 (std::make_shared<nano::state_block> (key1.pub, open1->hash (), key1.pub, nano::xrb_ratio * 9, key2.pub, key1.prv, key1.pub, *system.work.generate (open1->hash ())));
	auto send3 (std::make_shared<nano::state_block> (key1.pub, send2->hash (), key1.pub, nano::xrb_ratio * 8, key2.pub, key1.prv, key1.pub, *system.work.generate (send2->hash ())));
	auto send4 (std::make_shared<nano::state_block> (key1.pub, send3->hash (), key1.pub, nano::xrb_ratio * 7, key2.pub, key1.prv, key1.pub, *system.work.generate (send3->hash ())));
	auto send5 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 20 * nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send1->hash ())));
	auto send6 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send5->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 30 * nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send5->hash ())));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10 * nano::xrb_ratio, send5->hash (), key2.prv, key2.pub, *system.work.generate (key2.pub, nano::difficulty::from_multiplier (50., node1.network_params.network.publish_threshold))));
	uint64_t difficulty1 (0);
	nano::work_validate (*open2, &difficulty1);
	uint64_t difficulty2 (0);
	nano::work_validate (*send6, &difficulty2);

	node1.process_active (send1);
	node1.process_active (open1);
	node1.process_active (send5);
	system.deadline_set (10s);
	while (node1.active.size () != 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!node1.active.empty ())
	{
		nano::lock_guard<std::mutex> active_guard (node1.active.mutex);
		if (!node1.active.roots.empty ())
		{
			node1.active.roots.begin ()->election->confirm_once ();
		}
	}
	node1.process_active (send2);
	node1.process_active (send3);
	node1.process_active (send4);
	node1.process_active (send6);

	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	std::this_thread::sleep_for (1s);
	node1.process_active (open2);
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	size_t seen (0);
	{
		auto it (node1.active.roots.get<1> ().begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.get<1> ().end ())
		{
			if (it->difficulty == (difficulty1 || difficulty2))
			{
				seen++;
			}
			it++;
		}
	}
	ASSERT_LT (seen, 2);
	ASSERT_EQ (node1.active.size (), 4);
}

TEST (active_transactions, inactive_votes_cache)
{
	nano::system system (1);
	auto & node = *system.nodes[0];
	nano::block_hash latest (node.latest (nano::test_genesis_key.pub));
	nano::keypair key;
	auto send (std::make_shared<nano::send_block> (latest, key.pub, nano::genesis_amount - 100, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (latest)));
	auto vote (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, 0, std::vector<nano::block_hash> (1, send->hash ())));
	node.vote_processor.vote (vote, std::make_shared<nano::transport::channel_udp> (node.network.udp_channels, node.network.endpoint (), node.network_params.protocol.protocol_version));
	system.deadline_set (5s);
	while (node.active.inactive_votes_cache_size () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node.process_active (send);
	node.block_processor.flush ();
	bool confirmed (false);
	system.deadline_set (5s);
	while (!node.ledger.block_confirmed (node.store.tx_begin_read (), send->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (active_transactions, inactive_votes_cache_fork)
{
	nano::system system (1);
	auto & node = *system.nodes[0];
	nano::block_hash latest (node.latest (nano::test_genesis_key.pub));
	nano::keypair key;
	auto send1 (std::make_shared<nano::send_block> (latest, key.pub, nano::genesis_amount - 100, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (latest)));
	auto send2 (std::make_shared<nano::send_block> (latest, key.pub, nano::genesis_amount - 200, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (latest)));
	auto vote (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, 0, std::vector<nano::block_hash> (1, send1->hash ())));
	node.vote_processor.vote (vote, std::make_shared<nano::transport::channel_udp> (node.network.udp_channels, node.network.endpoint (), node.network_params.protocol.protocol_version));
	auto channel1 (node.network.udp_channels.create (node.network.endpoint ()));
	system.deadline_set (5s);
	while (node.active.inactive_votes_cache_size () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node.network.process_message (nano::publish (send2), channel1);
	node.block_processor.flush ();
	ASSERT_NE (nullptr, node.block (send2->hash ()));
	node.network.process_message (nano::publish (send1), channel1);
	node.block_processor.flush ();
	bool confirmed (false);
	system.deadline_set (5s);
	while (!confirmed)
	{
		auto transaction (node.store.tx_begin_read ());
		confirmed = node.block (send1->hash ()) != nullptr && node.ledger.block_confirmed (transaction, send1->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (active_transactions, inactive_votes_cache_existing_vote)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	nano::block_hash latest (node.latest (nano::test_genesis_key.pub));
	nano::keypair key;
	auto send (std::make_shared<nano::send_block> (latest, key.pub, nano::genesis_amount - 100 * nano::Gxrb_ratio, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (latest)));
	auto open (std::make_shared<nano::state_block> (key.pub, 0, key.pub, 100 * nano::Gxrb_ratio, send->hash (), key.prv, key.pub, *system.work.generate (key.pub))); // Increase key weight
	node.process_active (send);
	node.block_processor.add (open);
	node.block_processor.flush ();
	system.deadline_set (5s);
	while (node.active.size () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	std::shared_ptr<nano::election> election;
	{
		nano::lock_guard<std::mutex> active_guard (node.active.mutex);
		auto it (node.active.roots.begin ());
		ASSERT_NE (node.active.roots.end (), it);
		election = it->election;
	}
	ASSERT_GT (node.weight (key.pub), node.minimum_principal_weight ());
	// Insert vote
	auto vote1 (std::make_shared<nano::vote> (key.pub, key.prv, 1, std::vector<nano::block_hash> (1, send->hash ())));
	node.vote_processor.vote (vote1, std::make_shared<nano::transport::channel_udp> (node.network.udp_channels, node.network.endpoint (), node.network_params.protocol.protocol_version));
	system.deadline_set (5s);
	bool done (false);
	while (!done)
	{
		nano::unique_lock<std::mutex> active_lock (node.active.mutex);
		done = (election->last_votes.size () == 2);
		active_lock.unlock ();
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_new));
	nano::lock_guard<std::mutex> active_guard (node.active.mutex);
	auto last_vote1 (election->last_votes[key.pub]);
	ASSERT_EQ (send->hash (), last_vote1.hash);
	ASSERT_EQ (1, last_vote1.sequence);
	// Attempt to change vote with inactive_votes_cache
	node.active.add_inactive_votes_cache (send->hash (), key.pub);
	ASSERT_EQ (1, node.active.find_inactive_votes_cache (send->hash ()).voters.size ());
	election->insert_inactive_votes_cache (send->hash ());
	// Check that election data is not changed
	ASSERT_EQ (2, election->last_votes.size ());
	auto last_vote2 (election->last_votes[key.pub]);
	ASSERT_EQ (last_vote1.hash, last_vote2.hash);
	ASSERT_EQ (last_vote1.sequence, last_vote2.sequence);
	ASSERT_EQ (last_vote1.time, last_vote2.time);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (active_transactions, inactive_votes_cache_multiple_votes)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	nano::block_hash latest (node.latest (nano::test_genesis_key.pub));
	nano::keypair key1;
	auto send1 (std::make_shared<nano::send_block> (latest, key1.pub, nano::genesis_amount - 100 * nano::Gxrb_ratio, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (latest)));
	auto send2 (std::make_shared<nano::send_block> (send1->hash (), key1.pub, 100 * nano::Gxrb_ratio, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send1->hash ()))); // Decrease genesis weight to prevent election confirmation
	auto open (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 100 * nano::Gxrb_ratio, send1->hash (), key1.prv, key1.pub, *system.work.generate (key1.pub))); // Increase key1 weight
	node.block_processor.add (send1);
	node.block_processor.add (send2);
	node.block_processor.add (open);
	node.block_processor.flush ();
	// Process votes
	auto vote1 (std::make_shared<nano::vote> (key1.pub, key1.prv, 0, std::vector<nano::block_hash> (1, send1->hash ())));
	node.vote_processor.vote (vote1, std::make_shared<nano::transport::channel_udp> (node.network.udp_channels, node.network.endpoint (), node.network_params.protocol.protocol_version));
	auto vote2 (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, 0, std::vector<nano::block_hash> (1, send1->hash ())));
	node.vote_processor.vote (vote2, std::make_shared<nano::transport::channel_udp> (node.network.udp_channels, node.network.endpoint (), node.network_params.protocol.protocol_version));
	system.deadline_set (5s);
	while (true)
	{
		{
			nano::lock_guard<std::mutex> active_guard (node.active.mutex);
			if (node.active.find_inactive_votes_cache (send1->hash ()).voters.size () == 2)
			{
				break;
			}
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, node.active.inactive_votes_cache_size ());
	// Start election
	node.active.start (send1);
	{
		nano::lock_guard<std::mutex> active_guard (node.active.mutex);
		auto it (node.active.roots.begin ());
		ASSERT_NE (node.active.roots.end (), it);
		ASSERT_EQ (3, it->election->last_votes.size ()); // 2 votes and 1 default not_an_acount
	}
	ASSERT_EQ (2, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (active_transactions, update_difficulty)
{
	nano::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	nano::genesis genesis;
	nano::keypair key1;
	// Generate blocks & start elections
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 100, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	uint64_t difficulty1 (0);
	nano::work_validate (*send1, &difficulty1);
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 200, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (send1->hash ())));
	uint64_t difficulty2 (0);
	nano::work_validate (*send2, &difficulty2);
	node1.process_active (send1);
	node1.process_active (send2);
	node1.block_processor.flush ();
	system.deadline_set (10s);
	while (node1.active.size () != 2 || node2.active.size () != 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Update work with higher difficulty
	auto work1 = node1.work_generate_blocking (send1->root (), difficulty1 + 1, boost::none);
	auto work2 = node1.work_generate_blocking (send2->root (), difficulty2 + 1, boost::none);

	std::error_code ec;
	nano::state_block_builder builder;
	send1 = std::shared_ptr<nano::state_block> (builder.from (*send1).work (*work1).build (ec));
	nano::state_block_builder builder1;
	send2 = std::shared_ptr<nano::state_block> (builder1.from (*send2).work (*work2).build (ec));
	ASSERT_FALSE (ec);

	auto modify_election = [&node1](auto block) {
		auto hash (block->hash ());
		nano::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto existing (node1.active.roots.find (block->qualified_root ()));
		ASSERT_NE (existing, node1.active.roots.end ());
		auto election (existing->election);
		ASSERT_EQ (election->status.winner->hash (), hash);
		election->status.winner = block;
		auto current (election->blocks.find (hash));
		assert (current != election->blocks.end ());
		current->second = block;
	};

	modify_election (send1);
	modify_election (send2);
	node1.process_active (send1);
	node1.process_active (send2);
	node1.block_processor.flush ();
	system.deadline_set (10s);
	bool done (false);
	while (!done)
	{
		{
			// node1
			nano::lock_guard<std::mutex> guard1 (node1.active.mutex);
			auto const existing1 (node1.active.roots.find (send1->qualified_root ()));
			ASSERT_NE (existing1, node1.active.roots.end ());
			auto const existing2 (node1.active.roots.find (send2->qualified_root ()));
			ASSERT_NE (existing2, node1.active.roots.end ());
			// node2
			nano::lock_guard<std::mutex> guard2 (node2.active.mutex);
			auto const existing3 (node2.active.roots.find (send1->qualified_root ()));
			ASSERT_NE (existing3, node2.active.roots.end ());
			auto const existing4 (node2.active.roots.find (send2->qualified_root ()));
			ASSERT_NE (existing4, node2.active.roots.end ());
			auto updated = (existing1->difficulty > difficulty1) && (existing2->difficulty > difficulty2);
			auto propogated = (existing3->difficulty > difficulty1) && (existing4->difficulty > difficulty2);
			done = updated && propogated;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (active_transactions, restart_dropped)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	nano::genesis genesis;
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::xrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	// Process only in ledger and emulate dropping the election
	ASSERT_EQ (nano::process_result::progress, node.process (*send1).code);
	{
		nano::lock_guard<std::mutex> guard (node.active.mutex);
		node.active.add_dropped_elections_cache (send1->qualified_root ());
	}
	uint64_t difficulty1 (0);
	nano::work_validate (*send1, &difficulty1);
	// Generate higher difficulty work
	auto work2 (*system.work.generate (send1->root (), difficulty1));
	uint64_t difficulty2 (0);
	nano::work_validate (send1->root (), work2, &difficulty2);
	ASSERT_GT (difficulty2, difficulty1);
	// Process the same block with updated work
	auto send2 (std::make_shared<nano::state_block> (*send1));
	send2->block_work_set (work2);
	node.process_active (send2);
	// Wait until the block is in elections
	system.deadline_set (5s);
	bool done{ false };
	while (!done)
	{
		{
			nano::lock_guard<std::mutex> guard (node.active.mutex);
			auto existing (node.active.roots.find (send2->qualified_root ()));
			done = existing != node.active.roots.end ();
			if (done)
			{
				ASSERT_EQ (difficulty2, existing->difficulty);
			}
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	std::shared_ptr<nano::block> block;
	while (block == nullptr)
	{
		ASSERT_NO_ERROR (system.poll ());
		block = node.store.block_get (node.store.tx_begin_read (), send1->hash ());
	}
	ASSERT_EQ (work2, block->block_work ());
	// Drop election
	node.active.erase (*send2);
	// Try to restart election with the lower difficulty block, should not work since the block as lower work
	node.process_active (send1);
	system.deadline_set (5s);
	while (node.block_processor.size () > 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node.active.empty ());
	// Verify the block was not updated in the ledger
	{
		auto block (node.store.block_get (node.store.tx_begin_read (), send1->hash ()));
		ASSERT_EQ (work2, block->block_work ());
	}
}

TEST (active_transactions, vote_replays)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	nano::genesis genesis;
	nano::keypair key;
	std::error_code ec;
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::Gxrb_ratio, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	ASSERT_NE (nullptr, send1);
	auto open1 (std::make_shared<nano::state_block> (key.pub, 0, key.pub, nano::Gxrb_ratio, send1->hash (), key.prv, key.pub, *system.work.generate (key.pub)));
	ASSERT_NE (nullptr, open1);
	node.process_active (send1);
	node.process_active (open1);
	node.block_processor.flush ();
	ASSERT_EQ (2, node.active.size ());
	// First vote is not a replay and confirms the election, second vote should be indeterminate since the election no longer exists
	auto vote_send1 (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, 0, send1));
	ASSERT_EQ (nano::vote_code::vote, node.active.vote (vote_send1));
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (nano::vote_code::indeterminate, node.active.vote (vote_send1));
	// Open new account
	auto vote_open1 (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, 0, open1));
	ASSERT_EQ (nano::vote_code::vote, node.active.vote (vote_open1));
	ASSERT_TRUE (node.active.empty ());
	ASSERT_EQ (nano::vote_code::indeterminate, node.active.vote (vote_open1));
	ASSERT_EQ (nano::Gxrb_ratio, node.ledger.weight (key.pub));

	auto send2 (std::make_shared<nano::state_block> (key.pub, open1->hash (), key.pub, nano::Gxrb_ratio - 1, key.pub, key.prv, key.pub, *system.work.generate (open1->hash ())));
	ASSERT_NE (nullptr, send2);
	node.process_active (send2);
	node.block_processor.flush ();
	ASSERT_EQ (1, node.active.size ());
	auto vote1_send2 (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, 0, send2));
	auto vote2_send2 (std::make_shared<nano::vote> (key.pub, key.prv, 0, send2));
	ASSERT_EQ (nano::vote_code::vote, node.active.vote (vote2_send2));
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (nano::vote_code::replay, node.active.vote (vote2_send2));
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (nano::vote_code::vote, node.active.vote (vote1_send2));
	ASSERT_EQ (0, node.active.size ());
	ASSERT_EQ (nano::vote_code::indeterminate, node.active.vote (vote1_send2));
	ASSERT_EQ (nano::vote_code::indeterminate, node.active.vote (vote2_send2));
}
