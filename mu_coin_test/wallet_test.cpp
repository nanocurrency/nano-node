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
        mu_coin::EC::PrivateKey prv;
        mu_coin::point_encoding encoding (key);
        bool failed;
        i.data.key (secret, encoding.iv (), prv, failed);
        ASSERT_FALSE (failed);
        ASSERT_EQ (key1.prv.GetPrivateExponent (), prv.GetPrivateExponent ());
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
    std::vector <mu_coin::EC::PublicKey> keys1;
    std::vector <mu_coin::EC::PrivateKey> keys2;
    for (auto i (wallet.begin ()), j (wallet.end ()); i != j; ++i)
    {
        mu_coin::EC::PublicKey key (*i);
        mu_coin::EC::PrivateKey prv;
        mu_coin::point_encoding encoding (key);
        bool failed;
        i.data.key (secret, encoding.iv (), prv, failed);
        ASSERT_FALSE (failed);
        keys1.push_back (key);
        keys2.push_back (prv);
    }
    ASSERT_EQ (2, keys1.size ());
    ASSERT_EQ (2, keys2.size ());
    ASSERT_EQ (key1.pub, keys1 [0]);
    ASSERT_EQ (key1.prv.GetPrivateExponent (), keys2 [0].GetPrivateExponent ());
    ASSERT_EQ (key2.pub, keys1 [1]);
    ASSERT_EQ (key2.prv.GetPrivateExponent (), keys2 [1].GetPrivateExponent ());
}