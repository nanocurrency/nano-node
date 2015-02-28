#include <gtest/gtest.h>

#include <rai/node.hpp>

TEST (wallets, open_create)
{
    rai::system system (24000, 1);
    rai::wallets wallets (*system.nodes [0]);
    ASSERT_EQ (0, wallets.items.size ());
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
        rai::wallets wallets (*system.nodes [0]);
        ASSERT_EQ (0, wallets.items.size ());
        auto wallet (wallets.create (id));
        ASSERT_NE (nullptr, wallet);
        ASSERT_EQ (wallet, wallets.open (id));
		auto iterations (0);
		while (wallet->store.password.value () == 0)
		{
			system.service->poll_one ();
			system.processor.poll_one ();
			++iterations;
			ASSERT_LT (iterations, 200);
		}
    }
    {
        rai::wallets wallets (*system.nodes [0]);
        ASSERT_EQ (1, wallets.items.size ());
        ASSERT_NE (nullptr, wallets.open (id));
    }
}

TEST (wallets, remove)
{
    rai::system system (24000, 1);
    rai::uint256_union one (1);
    {
        rai::wallets wallets (*system.nodes [0]);
        ASSERT_EQ (0, wallets.items.size ());
        auto wallet (wallets.create (one));
        ASSERT_NE (nullptr, wallet);
        ASSERT_EQ (1, wallets.items.size ());
        wallets.destroy (one);
        ASSERT_EQ (0, wallets.items.size ());
    }
    {
        rai::wallets wallets (*system.nodes [0]);
        ASSERT_EQ (0, wallets.items.size ());
    }
}