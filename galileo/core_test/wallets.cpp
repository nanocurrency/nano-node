#include <gtest/gtest.h>

#include <galileo/core_test/testutil.hpp>
#include <galileo/node/testing.hpp>

using namespace std::chrono_literals;

TEST (wallets, open_create)
{
	galileo::system system (24000, 1);
	bool error (false);
	galileo::wallets wallets (error, *system.nodes[0]);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, wallets.items.size ()); // it starts out with a default wallet
	galileo::uint256_union id;
	ASSERT_EQ (nullptr, wallets.open (id));
	auto wallet (wallets.create (id));
	ASSERT_NE (nullptr, wallet);
	ASSERT_EQ (wallet, wallets.open (id));
}

TEST (wallets, open_existing)
{
	galileo::system system (24000, 1);
	galileo::uint256_union id;
	{
		bool error (false);
		galileo::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (id));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (wallet, wallets.open (id));
		galileo::raw_key password;
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
		galileo::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (2, wallets.items.size ());
		ASSERT_NE (nullptr, wallets.open (id));
	}
}

TEST (wallets, remove)
{
	galileo::system system (24000, 1);
	galileo::uint256_union one (1);
	{
		bool error (false);
		galileo::wallets wallets (error, *system.nodes[0]);
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
		galileo::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
	}
}

// Keeps breaking whenever we add new DBs
TEST (wallets, DISABLED_wallet_create_max)
{
	galileo::system system (24000, 1);
	bool error (false);
	galileo::wallets wallets (error, *system.nodes[0]);
	const int nonWalletDbs = 19;
	for (int i = 0; i < system.nodes[0]->config.lmdb_max_dbs - nonWalletDbs; i++)
	{
		galileo::keypair key;
		auto wallet = wallets.create (key.pub);
		auto existing = wallets.items.find (key.pub);
		ASSERT_TRUE (existing != wallets.items.end ());
		galileo::raw_key seed;
		seed.data = 0;
		auto transaction (system.nodes[0]->store.tx_begin (true));
		existing->second->store.seed_set (transaction, seed);
	}
	galileo::keypair key;
	wallets.create (key.pub);
	auto existing = wallets.items.find (key.pub);
	ASSERT_TRUE (existing == wallets.items.end ());
}
