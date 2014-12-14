#include <gtest/gtest.h>

#include <rai/core/core.hpp>

TEST (wallets, open_create)
{
    rai::wallets wallets (boost::filesystem::unique_path ());
    ASSERT_EQ (0, wallets.items.size ());
    rai::uint256_union id;
    ASSERT_EQ (nullptr, wallets.open (id));
    auto wallet (wallets.create (id));
    ASSERT_EQ (wallet, wallets.open (id));
}

TEST (wallets, open_existing)
{
    rai::uint256_union id;
    auto path (boost::filesystem::unique_path ());
    {
        rai::wallets wallets (path);
        ASSERT_EQ (0, wallets.items.size ());
        auto wallet (wallets.create (id));
        ASSERT_EQ (wallet, wallets.open (id));
    }
    {
        rai::wallets wallets (path);
        ASSERT_EQ (1, wallets.items.size ());
        ASSERT_NE (nullptr, wallets.open (id));
    }
}