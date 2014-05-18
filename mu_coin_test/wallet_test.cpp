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

TEST (wallet, empty_iteration)
{
    mu_coin_wallet::wallet wallet (mu_coin_wallet::wallet_temp);
    auto i (wallet.begin ());
    auto j (wallet.end ());
    ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
    mu_coin_wallet::wallet wallet (mu_coin_wallet::wallet_temp);
    mu_coin::keypair key1;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (key1.prv, secret);
    for (auto i (wallet.begin ()), j (wallet.end ()); i != j; ++i)
    {
        mu_coin::EC::PublicKey key (*i);
        ASSERT_EQ (key1.pub, key);
    }
}

TEST (wallet, two_item_iteration)
{
    mu_coin_wallet::wallet wallet (mu_coin_wallet::wallet_temp);
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (key1.prv, secret);
    wallet.insert (key2.prv, secret);
    std::vector <mu_coin::EC::PublicKey> keys;
    for (auto i (wallet.begin ()), j (wallet.end ()); i != j; ++i)
    {
        mu_coin::EC::PublicKey key (*i);
        keys.push_back (key);
    }
    ASSERT_EQ (2, keys.size ());
    ASSERT_EQ (key1.pub, keys [0]);
    ASSERT_EQ (key2.pub, keys [1]);
}