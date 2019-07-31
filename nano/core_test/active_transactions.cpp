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
	nano::node_flags node_flags;
	node_flags.delay_frontier_confirmation_height_updating = true;
	auto & node1 = *system.add_node (node_config, node_flags);
	nano::genesis genesis;
	nano::keypair key1, key2, key3;

	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - 10 * nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 20 * nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10 * nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10 * nano::xrb_ratio, send2->hash (), key2.prv, key2.pub, system.work.generate (key2.pub)));
	node1.process_active (send1);
	node1.process_active (send2);
	node1.process_active (open1);
	node1.process_active (open2);
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	while (node1.active.size () != 0)
	{
		std::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.end ())
		{
			auto election (it->election);
			election->confirm_once ();
			it = node1.active.roots.begin ();
		}
	}

	system.deadline_set (10s);
	while (node1.active.confirmed.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	//genesis and key1,key2 are opened
	//start chain of 2 on each
	auto send3 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send2->hash (), nano::test_genesis_key.pub, 9 * nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send2->hash (), nano::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send4 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send3->hash (), nano::test_genesis_key.pub, 8 * nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send3->hash (), nano::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send5 (std::make_shared<nano::state_block> (key1.pub, open1->hash (), key1.pub, 9 * nano::xrb_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (open1->hash (), nano::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send6 (std::make_shared<nano::state_block> (key1.pub, send5->hash (), key1.pub, 8 * nano::xrb_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (send5->hash (), nano::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send7 (std::make_shared<nano::state_block> (key2.pub, open2->hash (), key2.pub, 9 * nano::xrb_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (open2->hash (), nano::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));
	auto send8 (std::make_shared<nano::state_block> (key2.pub, send7->hash (), key2.pub, 8 * nano::xrb_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (send7->hash (), nano::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));

	node1.process_active (send3); //genesis
	node1.process_active (send5); //key1
	node1.process_active (send7); //key2
	node1.process_active (send4); //genesis
	node1.process_active (send6); //key1
	node1.process_active (send8); //key2

	system.deadline_set (10s);
	while (node1.active.size () != 6)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	std::lock_guard<std::mutex> lock (node1.active.mutex);
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
}

TEST (active_transactions, keep_local)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.active_elections_size = 3; //bound to 3, wont drop wallet created transactions, but good to test dropping remote
	//delay_frontier_confirmation_height_updating to allow the test to before
	nano::node_flags node_flags;
	node_flags.delay_frontier_confirmation_height_updating = true;
	auto & node1 = *system.add_node (node_config, node_flags);
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
		std::lock_guard<std::mutex> active_guard (node1.active.mutex);
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
	//delay_frontier_confirmation_height_updating to allow the test to before
	nano::node_flags node_flags;
	node_flags.delay_frontier_confirmation_height_updating = true;
	auto & node1 = *system.add_node (node_config, node_flags);
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
		std::lock_guard<std::mutex> active_guard (node1.active.mutex);
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
			std::lock_guard<std::mutex> guard (node1.active.mutex);
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
			std::lock_guard<std::mutex> guard (node1.active.mutex);
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
