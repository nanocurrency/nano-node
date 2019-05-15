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
