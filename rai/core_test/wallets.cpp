#include <gtest/gtest.h>

#include <rai/node/testing.hpp>

TEST (wallets, DISABLED_open_create)
{
	rai::system system (24000, 1);
	bool error (false);
	rai::wallets wallets (error, *system.nodes[0]);
	ASSERT_FALSE (error);
	ASSERT_EQ (0, wallets.items.size ());
	rai::uint256_union id;
	ASSERT_EQ (nullptr, wallets.open (id));
	auto wallet (wallets.create (id));
	ASSERT_NE (nullptr, wallet);
	ASSERT_EQ (wallet, wallets.open (id));
}

TEST (wallets, DISABLED_open_existing)
{
	rai::system system (24000, 1);
	rai::uint256_union id;
	{
		bool error (false);
		rai::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (0, wallets.items.size ());
		auto wallet (wallets.create (id));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (wallet, wallets.open (id));
		auto iterations (0);
		rai::raw_key password;
		password.data.clear ();
		while (password.data == 0)
		{
			system.poll ();
			++iterations;
			ASSERT_LT (iterations, 200);
			wallet->store.password.value (password);
		}
	}
	{
		bool error (false);
		rai::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		ASSERT_NE (nullptr, wallets.open (id));
	}
}

TEST (wallets, DISABLED_remove)
{
	rai::system system (24000, 1);
	rai::uint256_union one (1);
	{
		bool error (false);
		rai::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (0, wallets.items.size ());
		auto wallet (wallets.create (one));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (1, wallets.items.size ());
		wallets.destroy (one);
		ASSERT_EQ (0, wallets.items.size ());
	}
	{
		bool error (false);
		rai::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (0, wallets.items.size ());
	}
}