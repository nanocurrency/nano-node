#include <gtest/gtest.h>
#include <mu_coin_wallet/wallet.hpp>

TEST (wallet, no_key)
{
    mu_coin_wallet::wallet wallet (mu_coin_wallet::wallet_temp);
    mu_coin::keypair key1;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    mu_coin::EC::PrivateKey prv1;
    bool failure;
    wallet.fetch (key1.pub, secret, prv1, failure);
    ASSERT_TRUE (failure);
}

TEST (wallet, retrieval)
{
    mu_coin_wallet::wallet wallet (mu_coin_wallet::wallet_temp);
    mu_coin::keypair key1;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (key1.prv, secret);
    mu_coin::EC::PrivateKey prv1;
    bool failure1;
    wallet.fetch (key1.pub, secret, prv1, failure1);
    ASSERT_FALSE (failure1);
    ASSERT_EQ (key1.prv.GetPrivateExponent (), prv1.GetPrivateExponent ());
    secret.bytes [16] ^= 1;
    mu_coin::EC::PrivateKey prv2;
    bool failure2;
    wallet.fetch (key1.pub, secret, prv2, failure2);
    ASSERT_TRUE (failure2);
}