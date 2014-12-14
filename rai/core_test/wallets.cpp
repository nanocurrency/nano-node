#include <gtest/gtest.h>

#include <rai/core/core.hpp>

TEST (wallets, open_create)
{
    rai::wallets wallets (boost::filesystem::unique_path ());
    rai::uint256_union id;
    ASSERT_EQ (nullptr, wallets.open (id));
    auto wallet (wallets.create (id));
    ASSERT_EQ (wallet, wallets.open (id));
}