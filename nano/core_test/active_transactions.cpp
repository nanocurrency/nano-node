#include <nano/core_test/testutil.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (active_transactions, bounded_active_elections)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.active_elections_size = 5;
	auto & node1 = *system.add_node (node_config);
	nano::genesis genesis;
	size_t count (1);
	auto send (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - count * nano::xrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto previous_size = node1.active.size ();
	bool done (false);
	system.deadline_set (5s);
	while (!done)
	{
		count++;
		node1.process_active (send);
		done = previous_size > node1.active.size ();
		ASSERT_LT (node1.active.size (), node1.config.active_elections_size); //triggers after reverting #2116
		ASSERT_NO_ERROR (system.poll ());
		auto previous_hash = send->hash ();
		send = std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous_hash, nano::test_genesis_key.pub, nano::genesis_amount - count * nano::xrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous_hash));
		previous_size = node1.active.size ();
		//sleep this thread for the max delay between request loop rounds possible for such a small active_elections_size
		std::this_thread::sleep_for (std::chrono::milliseconds (node1.network_params.network.request_interval_ms + (node_config.active_elections_size * 20)));
	}
}

TEST (active_transactions, adjusted_difficulty_priority)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::genesis genesis;
	nano::keypair key1, key2, key3;

	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 10 * nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 20 * nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10 * nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10 * nano::xrb_ratio, send2->hash (), key2.prv, key2.pub, system.work.generate (key2.pub)));
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
	while (node1.active.size () != 0)
	{
		nano::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.end ())
		{
			auto election (it->election);
			election->confirm_once ();
			it = node1.active.roots.begin ();
		}
	}
	{
		system.deadline_set (10s);
		nano::unique_lock<std::mutex> active_lock (node1.active.mutex);
		while (node1.active.confirmed.size () != 4)
		{
			active_lock.unlock ();
			ASSERT_NO_ERROR (system.poll ());
			active_lock.lock ();
		}
	}

	//genesis and key1,key2 are opened
	//start chain of 2 on each
	auto send3 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send2->hash (), nano::test_genesis_key.pub, 9 * nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send2->hash (), nano::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send4 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send3->hash (), nano::test_genesis_key.pub, 8 * nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send3->hash (), nano::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send5 (std::make_shared<nano::state_block> (key1.pub, open1->hash (), key1.pub, 9 * nano::xrb_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (open1->hash (), nano::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send6 (std::make_shared<nano::state_block> (key1.pub, send5->hash (), key1.pub, 8 * nano::xrb_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (send5->hash (), nano::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send7 (std::make_shared<nano::state_block> (key2.pub, open2->hash (), key2.pub, 9 * nano::xrb_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (open2->hash (), nano::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));
	auto send8 (std::make_shared<nano::state_block> (key2.pub, send7->hash (), key2.pub, 8 * nano::xrb_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (send7->hash (), nano::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));

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
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::genesis genesis;
	nano::keypair key1, key2;

	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 10 * nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 20 * nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10 * nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10 * nano::xrb_ratio, send2->hash (), key2.prv, key2.pub, system.work.generate (key2.pub)));
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
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::genesis genesis;
	nano::keypair key1, key2, key3;

	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 10 * nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 20 * nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10 * nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10 * nano::xrb_ratio, send2->hash (), key2.prv, key2.pub, system.work.generate (key2.pub)));
	auto send3 (std::make_shared<nano::state_block> (key2.pub, open2->hash (), key2.pub, 9 * nano::xrb_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (open2->hash ())));
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
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.active_elections_size = 3; //bound to 3, wont drop wallet created transactions, but good to test dropping remote
	// Disable frontier confirmation to allow the test to finish before
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	nano::genesis genesis;
	//key 1/2 will be managed by the wallet
	nano::keypair key1, key2, key3, key4;
	wallet.insert_adhoc (nano::test_genesis_key.prv);
	wallet.insert_adhoc (key1.prv);
	wallet.insert_adhoc (key2.prv);
	auto send1 (wallet.send_action (nano::test_genesis_key.pub, key1.pub, node1.config.receive_minimum.number ()));
	auto send2 (wallet.send_action (nano::test_genesis_key.pub, key2.pub, node1.config.receive_minimum.number ()));
	auto send3 (wallet.send_action (nano::test_genesis_key.pub, key3.pub, node1.config.receive_minimum.number ()));
	auto send4 (wallet.send_action (nano::test_genesis_key.pub, key4.pub, node1.config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (node1.active.size () != 0)
	{
		nano::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.end ())
		{
			(it->election)->confirm_once ();
			it = node1.active.roots.begin ();
		}
	}
	auto open1 (std::make_shared<nano::state_block> (key3.pub, 0, key3.pub, nano::xrb_ratio, send3->hash (), key3.prv, key3.pub, system.work.generate (key3.pub)));
	node1.process_active (open1);
	auto open2 (std::make_shared<nano::state_block> (key4.pub, 0, key4.pub, nano::xrb_ratio, send4->hash (), key4.prv, key4.pub, system.work.generate (key4.pub)));
	node1.process_active (open2);
	//none are dropped since none are long_unconfirmed
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto send5 (wallet.send_action (nano::test_genesis_key.pub, key1.pub, node1.config.receive_minimum.number ()));
	node1.active.start (send5);
	//drop two lowest non-wallet managed active_transactions before inserting a new into active as all are long_unconfirmed
	system.deadline_set (10s);
	while (node1.active.size () != 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (active_transactions, prioritize_chains)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.active_elections_size = 4; //bound to 3, wont drop wallet created transactions, but good to test dropping remote
	// Disable frontier confirmation to allow the test to finish before
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::genesis genesis;
	nano::keypair key1, key2, key3;

	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 10 * nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10 * nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto send2 (std::make_shared<nano::state_block> (key1.pub, open1->hash (), key1.pub, nano::xrb_ratio * 9, key2.pub, key1.prv, key1.pub, system.work.generate (open1->hash ())));
	auto send3 (std::make_shared<nano::state_block> (key1.pub, send2->hash (), key1.pub, nano::xrb_ratio * 8, key2.pub, key1.prv, key1.pub, system.work.generate (send2->hash ())));
	auto send4 (std::make_shared<nano::state_block> (key1.pub, send3->hash (), key1.pub, nano::xrb_ratio * 7, key2.pub, key1.prv, key1.pub, system.work.generate (send3->hash ())));
	auto send5 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 20 * nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto send6 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send5->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 30 * nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send5->hash ())));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10 * nano::xrb_ratio, send5->hash (), key2.prv, key2.pub, system.work.generate (key2.pub, nano::difficulty::from_multiplier (50., node1.network_params.network.publish_threshold))));
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
	while (node1.active.size () != 0)
	{
		nano::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.get<1> ().begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.get<1> ().end ())
		{
			auto election (it->election);
			election->confirm_once ();
			it = node1.active.roots.get<1> ().begin ();
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
	bool done (false);
	//wait for all to be long_unconfirmed
	while (!done)
	{
		{
			nano::lock_guard<std::mutex> guard (node1.active.mutex);
			done = node1.active.long_unconfirmed_size == 4;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	std::this_thread::sleep_for (1s);
	node1.process_active (open2);
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	//wait for all to be long_unconfirmed
	done = false;
	system.deadline_set (10s);
	while (!done)
	{
		{
			nano::lock_guard<std::mutex> guard (node1.active.mutex);
			done = node1.active.long_unconfirmed_size == 4;
		}
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
	nano::system system (24000, 1);
	nano::block_hash latest (system.nodes[0]->latest (nano::test_genesis_key.pub));
	nano::keypair key;
	auto send (std::make_shared<nano::send_block> (latest, key.pub, nano::genesis_amount - 100, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (latest)));
	auto vote (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, 0, std::vector<nano::block_hash> (1, send->hash ())));
	system.nodes[0]->vote_processor.vote (vote, std::make_shared<nano::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint (), system.nodes[0]->network_params.protocol.protocol_version));
	system.deadline_set (5s);
	while (system.nodes[0]->active.inactive_votes_cache_size () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.nodes[0]->process_active (send);
	system.nodes[0]->block_processor.flush ();
	bool confirmed (false);
	system.deadline_set (5s);
	while (!confirmed)
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		confirmed = system.nodes[0]->ledger.block_confirmed (transaction, send->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, system.nodes[0]->stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (active_transactions, inactive_votes_cache_existing_vote)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);
	nano::block_hash latest (node->latest (nano::test_genesis_key.pub));
	nano::keypair key;
	auto send (std::make_shared<nano::send_block> (latest, key.pub, nano::genesis_amount - 100 * nano::Gxrb_ratio, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (latest)));
	auto open (std::make_shared<nano::state_block> (key.pub, 0, key.pub, 100 * nano::Gxrb_ratio, send->hash (), key.prv, key.pub, system.work.generate (key.pub))); // Increase key weight
	node->process_active (send);
	node->block_processor.add (open);
	node->block_processor.flush ();
	system.deadline_set (5s);
	while (node->active.size () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	std::shared_ptr<nano::election> election;
	{
		nano::lock_guard<std::mutex> active_guard (node->active.mutex);
		auto it (node->active.roots.begin ());
		ASSERT_NE (node->active.roots.end (), it);
		election = it->election;
	}
	ASSERT_GT (node->weight (key.pub), node->minimum_principal_weight ());
	// Insert vote
	auto vote1 (std::make_shared<nano::vote> (key.pub, key.prv, 1, std::vector<nano::block_hash> (1, send->hash ())));
	node->vote_processor.vote (vote1, std::make_shared<nano::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint (), system.nodes[0]->network_params.protocol.protocol_version));
	system.deadline_set (5s);
	bool done (false);
	while (!done)
	{
		nano::unique_lock<std::mutex> active_lock (node->active.mutex);
		done = (election->last_votes.size () == 2);
		active_lock.unlock ();
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, system.nodes[0]->stats.count (nano::stat::type::election, nano::stat::detail::vote_new));
	nano::lock_guard<std::mutex> active_guard (node->active.mutex);
	auto last_vote1 (election->last_votes[key.pub]);
	ASSERT_EQ (send->hash (), last_vote1.hash);
	ASSERT_EQ (1, last_vote1.sequence);
	// Attempt to change vote with inactive_votes_cache
	node->active.add_inactive_votes_cache (send->hash (), key.pub);
	ASSERT_EQ (1, node->active.find_inactive_votes_cache (send->hash ()).voters.size ());
	election->insert_inactive_votes_cache ();
	// Check that election data is not changed
	ASSERT_EQ (2, election->last_votes.size ());
	auto last_vote2 (election->last_votes[key.pub]);
	ASSERT_EQ (last_vote1.hash, last_vote2.hash);
	ASSERT_EQ (last_vote1.sequence, last_vote2.sequence);
	ASSERT_EQ (last_vote1.time, last_vote2.time);
	ASSERT_EQ (0, system.nodes[0]->stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (active_transactions, inactive_votes_cache_multiple_votes)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);
	nano::block_hash latest (system.nodes[0]->latest (nano::test_genesis_key.pub));
	nano::keypair key1;
	auto send1 (std::make_shared<nano::send_block> (latest, key1.pub, nano::genesis_amount - 100 * nano::Gxrb_ratio, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (latest)));
	auto send2 (std::make_shared<nano::send_block> (send1->hash (), key1.pub, 100 * nano::Gxrb_ratio, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ()))); // Decrease genesis weight to prevent election confirmation
	auto open (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 100 * nano::Gxrb_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub))); // Increase key1 weight
	node->block_processor.add (send1);
	node->block_processor.add (send2);
	node->block_processor.add (open);
	node->block_processor.flush ();
	// Process votes
	auto vote1 (std::make_shared<nano::vote> (key1.pub, key1.prv, 0, std::vector<nano::block_hash> (1, send1->hash ())));
	system.nodes[0]->vote_processor.vote (vote1, std::make_shared<nano::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint (), system.nodes[0]->network_params.protocol.protocol_version));
	auto vote2 (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, 0, std::vector<nano::block_hash> (1, send1->hash ())));
	system.nodes[0]->vote_processor.vote (vote2, std::make_shared<nano::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint (), system.nodes[0]->network_params.protocol.protocol_version));
	system.deadline_set (5s);
	while (system.nodes[0]->active.find_inactive_votes_cache (send1->hash ()).voters.size () != 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, system.nodes[0]->active.inactive_votes_cache_size ());
	// Start election
	system.nodes[0]->active.start (send1);
	{
		nano::lock_guard<std::mutex> active_guard (system.nodes[0]->active.mutex);
		auto it (system.nodes[0]->active.roots.begin ());
		ASSERT_NE (system.nodes[0]->active.roots.end (), it);
		ASSERT_EQ (3, it->election->last_votes.size ()); // 2 votes and 1 default not_an_acount
	}
	ASSERT_EQ (2, system.nodes[0]->stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (active_transactions, update_difficulty)
{
	nano::system system (24000, 2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	nano::genesis genesis;
	nano::keypair key1;
	// Generate blocks & start elections
	auto send1 (std::make_shared<nano::send_block> (genesis.hash (), key1.pub, nano::genesis_amount - 100, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	uint64_t difficulty1 (0);
	nano::work_validate (*send1, &difficulty1);
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 200, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ())));
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
	node1.work_generate_blocking (*send1, difficulty1 + 1);
	node1.work_generate_blocking (*send2, difficulty2 + 1);
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
			done = (existing1->difficulty > difficulty1) && (existing2->difficulty > difficulty2) && (existing3->difficulty > difficulty1) && (existing4->difficulty > difficulty2);
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}
