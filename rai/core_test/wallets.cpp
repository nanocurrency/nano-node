#include <gtest/gtest.h>

#include <rai/core_test/testutil.hpp>
#include <rai/node/testing.hpp>

using namespace std::chrono_literals;

TEST (wallets, open_create)
{
	rai::system system (24000, 1);
	bool error (false);
	rai::wallets wallets (error, *system.nodes[0]);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, wallets.items.size ()); // it starts out with a default wallet
	rai::uint256_union id;
	ASSERT_EQ (nullptr, wallets.open (id));
	auto wallet (wallets.create (id));
	ASSERT_NE (nullptr, wallet);
	ASSERT_EQ (wallet, wallets.open (id));
}

TEST (wallets, open_existing)
{
	rai::system system (24000, 1);
	rai::uint256_union id;
	{
		bool error (false);
		rai::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (id));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (wallet, wallets.open (id));
		rai::raw_key password;
		password.data.clear ();
		system.deadline_set (10s);
		while (password.data == 0)
		{
			ASSERT_NO_ERROR (system.poll ());
			wallet->store.password.value (password);
		}
	}
	{
		bool error (false);
		rai::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (2, wallets.items.size ());
		ASSERT_NE (nullptr, wallets.open (id));
	}
}

TEST (wallets, remove)
{
	rai::system system (24000, 1);
	rai::uint256_union one (1);
	{
		bool error (false);
		rai::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (one));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (2, wallets.items.size ());
		wallets.destroy (one);
		ASSERT_EQ (1, wallets.items.size ());
	}
	{
		bool error (false);
		rai::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
	}
}

// Keeps breaking whenever we add new DBs
TEST (wallets, DISABLED_wallet_create_max)
{
	rai::system system (24000, 1);
	bool error (false);
	rai::wallets wallets (error, *system.nodes[0]);
	const int nonWalletDbs = 19;
	for (int i = 0; i < system.nodes[0]->config.lmdb_max_dbs - nonWalletDbs; i++)
	{
		rai::keypair key;
		auto wallet = wallets.create (key.pub);
		auto existing = wallets.items.find (key.pub);
		ASSERT_TRUE (existing != wallets.items.end ());
		rai::raw_key seed;
		seed.data = 0;
		auto transaction (system.nodes[0]->store.tx_begin (true));
		existing->second->store.seed_set (transaction, seed);
	}
	rai::keypair key;
	wallets.create (key.pub);
	auto existing = wallets.items.find (key.pub);
	ASSERT_TRUE (existing == wallets.items.end ());
}

TEST (wallets, vote_minimum)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	bool error (false);
	rai::wallets wallets (error, node1);
	ASSERT_FALSE (error);
	rai::keypair key1;
	rai::keypair key2;
	rai::genesis genesis;
	rai::state_block send1 (rai::test_genesis_key.pub, genesis.hash (), rai::test_genesis_key.pub, std::numeric_limits<rai::uint128_t>::max () - node1.config.vote_minimum.number (), key1.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (genesis.hash ()));
	ASSERT_EQ (rai::process_result::progress, node1.process (send1).code);
	rai::state_block open1 (key1.pub, 0, key1.pub, node1.config.vote_minimum.number (), send1.hash (), key1.prv, key1.pub, system.work.generate (key1.pub));
	ASSERT_EQ (rai::process_result::progress, node1.process (open1).code);
	// send2 with amount vote_minimum - 1 (not voting representative)
	rai::state_block send2 (rai::test_genesis_key.pub, send1.hash (), rai::test_genesis_key.pub, std::numeric_limits<rai::uint128_t>::max () - 2 * node1.config.vote_minimum.number () + 1, key2.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (send1.hash ()));
	ASSERT_EQ (rai::process_result::progress, node1.process (send2).code);
	rai::state_block open2 (key2.pub, 0, key2.pub, node1.config.vote_minimum.number () - 1, send2.hash (), key2.prv, key2.pub, system.work.generate (key2.pub));
	ASSERT_EQ (rai::process_result::progress, node1.process (open2).code);
	auto wallet (wallets.items.begin ()->second);
	ASSERT_EQ (0, wallet->representatives.size ());
	wallet->insert_adhoc (rai::test_genesis_key.prv);
	wallet->insert_adhoc (key1.prv);
	wallet->insert_adhoc (key2.prv);
	wallets.compute_reps ();
	ASSERT_EQ (2, wallet->representatives.size ());
}
