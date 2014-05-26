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
    std::vector <CryptoPP::Integer> keys2;
    for (auto i (wallet.begin ()), j (wallet.end ()); i != j; ++i)
    {
        mu_coin::EC::PublicKey key (*i);
        mu_coin::EC::PrivateKey prv;
        mu_coin::point_encoding encoding (key);
        bool failed;
        i.data.key (secret, encoding.iv (), prv, failed);
        ASSERT_FALSE (failed);
        keys1.push_back (key);
        keys2.push_back (prv.GetPrivateExponent ());
    }
    ASSERT_EQ (2, keys1.size ());
    ASSERT_EQ (2, keys2.size ());
    ASSERT_NE (keys1.end (), std::find (keys1.begin (), keys1.end (), key1.pub));
    ASSERT_NE (keys2.end (), std::find (keys2.begin (), keys2.end (), key1.prv.GetPrivateExponent ()));
    ASSERT_NE (keys1.end (), std::find (keys1.begin (), keys1.end (), key2.pub));
    ASSERT_NE (keys2.end (), std::find (keys2.begin (), keys2.end (), key2.prv.GetPrivateExponent ()));
}

TEST (wallet, insufficient_spend)
{
    mu_coin_wallet::wallet wallet (mu_coin_wallet::wallet_temp);
    mu_coin::block_store_memory store;
    mu_coin::ledger ledger (store);
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    auto send (wallet.send (ledger, mu_coin::address (key1.pub), 500, password));
    ASSERT_EQ (nullptr, send);
}

TEST (wallet, one_spend)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    mu_coin_wallet::wallet wallet (mu_coin_wallet::wallet_temp);
    wallet.insert (key1.pub, key1.prv, password);
    mu_coin::block_store_memory store;
    mu_coin::ledger ledger (store);
    mu_coin::transaction_block block1;
    mu_coin::entry entry1 (key1.pub, 500, 0);
    block1.entries.push_back (entry1);
    store.insert_block (entry1.id, block1);
    mu_coin::keypair key2;
    mu_coin::address address1 (key2.pub);
    auto send (wallet.send (ledger, address1, 499, password));
    ASSERT_NE (nullptr, send);
    ASSERT_EQ (1, send->inputs.size ());
    ASSERT_EQ (1, send->outputs.size ());
    ASSERT_EQ (entry1.id.address, send->inputs [0].source.address);
    ASSERT_TRUE (send->inputs [0].validate (send->hash ()));
    ASSERT_EQ (address1, send->outputs [0].address);
}