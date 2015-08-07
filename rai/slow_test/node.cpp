#include <gtest/gtest.h>
#include <rai/node.hpp>

#include <thread>

TEST (system, generate_mass_activity)
{
    rai::system system (24000, 1);
    system.wallet (0)->insert (rai::test_genesis_key.prv);
    size_t count (20);
    system.generate_mass_activity (count, *system.nodes [0]);
    size_t accounts (0);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    for (auto i (system.nodes [0]->store.latest_begin (transaction)), n (system.nodes [0]->store.latest_end ()); i != n; ++i)
    {
        ++accounts;
    }
    ASSERT_GT (accounts, count / 10);
}

TEST (system, generate_mass_activity_long)
{
	std::vector <std::thread> threads;
	{
		rai::system system (24000, 1);
		for (auto i (0), n (4); i != n; ++i)
		{
			threads.push_back (std::thread ([&system] ()
			{
				system.service->run ();
			}));
		}
		for (auto i (0), n (4); i != n; ++i)
		{
			threads.push_back (std::thread ([&system] ()
			{
				system.processor.run ();
			}));
		}
		system.wallet (0)->insert (rai::test_genesis_key.prv);
		size_t count (10000);
		system.generate_mass_activity (count, *system.nodes [0]);
		size_t accounts (0);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		for (auto i (system.nodes [0]->store.latest_begin (transaction)), n (system.nodes [0]->store.latest_end ()); i != n; ++i)
		{
			++accounts;
		}
		ASSERT_GT (accounts, count / 10);
	}
	for (auto i (threads.begin ()), n (threads.end ()); i != n; ++i)
	{
		i->join ();
	}
}

TEST (ledger, deep_account_compute)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::ledger ledger (store);
	rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
	genesis.initialize (transaction, store);
	rai::keypair key;
	auto balance (rai::genesis_amount - 1);
	rai::send_block send (genesis.hash (), key.pub, balance, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send).code);
	rai::open_block open (send.hash (), rai::test_genesis_key.pub, key.pub, key.prv, key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, open).code);
	auto sprevious (send.hash ());
	auto rprevious (open.hash ());
	for (auto i (0), n (100000); i != n; ++i)
	{
		balance -= 1;
		rai::send_block send (sprevious, key.pub, balance, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send).code);
		sprevious = send.hash ();
		rai::receive_block receive (rprevious, send.hash (), key.prv, key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, receive).code);
		rprevious = receive.hash ();
		if (i % 100 == 0)
		{
			std::cerr << i << ' ';
		}
		auto account (ledger.account (transaction, sprevious));
		auto balance (ledger.balance (transaction, rprevious));
	}
}