#include <gtest/gtest.h>
#include <nano/core_test/testutil.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/node/testing.hpp>

TEST (transaction_counter, validate)
{
	auto now = std::chrono::steady_clock::now();
	nano::transaction_counter counter(now);
	auto count (0);
	ASSERT_EQ (count, counter.rate);
	while (std::chrono::steady_clock::now() < now + 1s)
	{
		count++;
		counter.add();
	}
	counter.trend_sample();
	ASSERT_EQ (count, counter.rate);
}

TEST (active_transactions, long_unconfirmed_size)
{
	nano::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	auto & wallet (*system.wallet (0));
	// disable voting to ensure blocks remain unconfirmed;
	node1.config.enable_voting = false;
	nano::genesis genesis;
	wallet.insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key1;
	auto send1 (wallet.send_action (nano::test_genesis_key.pub, nano::test_genesis_key.pub, nano::Mxrb_ratio));
	auto send2 (wallet.send_action (nano::test_genesis_key.pub, nano::test_genesis_key.pub, nano::Mxrb_ratio));
	auto send3 (wallet.send_action (nano::test_genesis_key.pub, nano::test_genesis_key.pub, nano::Mxrb_ratio));
	system.deadline_set (10s);
	while (node1.active.size () != 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto done (false);
	while (!done)
	{
		ASSERT_FALSE (node1.active.empty ());
		{
			std::lock_guard<std::mutex> guard (node1.active.mutex);
			auto info (node1.active.roots.find (nano::qualified_root (send1->hash (), send1->hash ())));
			ASSERT_NE (node1.active.roots.end (), info);
			done = info->election->announcements > nano::active_transactions::announcement_long;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	//since send1 is long_unconfirmed the other two should be as well
	ASSERT_EQ (node1.active.long_unconfirmed_size (), 3);
	{
			std::lock_guard<std::mutex> guard (node1.active.mutex);
			auto existing (node1.active.roots.find (send1->qualified_root ()));
			ASSERT_NE (node1.active.roots.end (), existing);
			//force election to appear confirmed
			(existing->election)->confirmed = true;
	}
	//only 2 should appear unconfirmed now
	ASSERT_EQ (node1.active.long_unconfirmed_size (), 2);
}

TEST (active_transactions, adjusted_difficulty_priority)
{
	nano::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	auto & wallet (*system.wallet (0));
	node1.config.enable_voting = false;
	nano::genesis genesis;
	nano::keypair key1, key2, key3;
	auto transaction (node1.store.tx_begin_read());
	
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash(), nano::test_genesis_key.pub, nano::genesis_amount-10*nano::xrb_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (genesis.hash())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash(), nano::test_genesis_key.pub, nano::genesis_amount-20*nano::xrb_ratio, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash())));
	auto open1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, 10*nano::xrb_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto open2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, 10*nano::xrb_ratio, send2->hash (), key2.prv, key2.pub, system.work.generate (key2.pub)));
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
		auto it(node1.active.roots.begin());
		while (!node1.active.roots.empty() && it != node1.active.roots.end ())
		{
			auto election (it->election);
			election->confirm_once();
			it++;;
		}
	}

	while (node1.active.confirmed.size() !=4)
	{
		ASSERT_NO_ERROR(system.poll());
	}

	//genesis and key1,key2 are opened
	//start chain of 2 on each
	auto send3 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send2->hash(), nano::test_genesis_key.pub, 9*nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send2->hash(), nano::difficulty::from_multiplier(1500,node1.network_params.network.publish_threshold))));
	auto send4 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send3->hash(), nano::test_genesis_key.pub, 8*nano::xrb_ratio, key3.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send3->hash(), nano::difficulty::from_multiplier(1500,node1.network_params.network.publish_threshold))));
	auto send5 (std::make_shared<nano::state_block> (key1.pub, open1->hash(), key1.pub, 9*nano::xrb_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (open1->hash(), nano::difficulty::from_multiplier(100,node1.network_params.network.publish_threshold))));
	auto send6 (std::make_shared<nano::state_block> (key1.pub, send5->hash(), key1.pub, 8*nano::xrb_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (send5->hash(), nano::difficulty::from_multiplier(100,node1.network_params.network.publish_threshold))));
	auto send7 (std::make_shared<nano::state_block> (key2.pub, open2->hash(), key2.pub, 9*nano::xrb_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (open2->hash(), nano::difficulty::from_multiplier(500,node1.network_params.network.publish_threshold))));
	auto send8 (std::make_shared<nano::state_block> (key2.pub, send7->hash(), key2.pub, 8*nano::xrb_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (send7->hash(), nano::difficulty::from_multiplier(500,node1.network_params.network.publish_threshold))));
	
	node1.process_active (send3); //genesis
	node1.process_active (send5); //key1
	node1.process_active (send7); //key2
	node1.process_active (send4); //genesis
	node1.process_active (send6); //key1
	node1.process_active (send8); //key2

	while (node1.active.size () != 6)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	{
		std::lock_guard<std::mutex> lock (node1.active.mutex);
		uint64_t last_adjusted(0);
		for (auto i (node1.active.roots.get<1> ().begin ()), n (node1.active.roots.get<1> ().end ()); i != n; ++i)
		{
			//first root has nothing to compare
			if(last_adjusted != 0)
			{
				ASSERT_LT (i->adjusted_difficulty, last_adjusted);
			}
			last_adjusted = i->adjusted_difficulty;
		}
	}
}
