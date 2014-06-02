#include <gtest/gtest.h>

#include <mu_coin/mu_coin.hpp>

TEST (wallet, no_key)
{
    mu_coin::wallet wallet (mu_coin::wallet_temp);
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
    mu_coin::wallet wallet (mu_coin::wallet_temp);
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
    mu_coin::wallet wallet (mu_coin::wallet_temp);
    auto i (wallet.begin ());
    auto j (wallet.end ());
    ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
    mu_coin::wallet wallet (mu_coin::wallet_temp);
    mu_coin::keypair key1;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (key1.prv, secret);
    for (auto i (wallet.begin ()), j (wallet.end ()); i != j; ++i)
    {
        mu_coin::EC::PublicKey key (*i);
        ASSERT_EQ (key1.pub, key);
        mu_coin::EC::PrivateKey prv;
        bool y;
        mu_coin::uint256_union encoding (key, y);
        bool failed;
        i.data.key (secret, encoding.owords [0], prv, failed);
        ASSERT_FALSE (failed);
        ASSERT_EQ (key1.prv.GetPrivateExponent (), prv.GetPrivateExponent ());
    }
}

TEST (wallet, two_item_iteration)
{
    mu_coin::wallet wallet (mu_coin::wallet_temp);
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
        bool y;
        mu_coin::uint256_union encoding (key, y);
        bool failed;
        i.data.key (secret, encoding.owords [0], prv, failed);
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
    mu_coin::wallet wallet (mu_coin::wallet_temp);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    auto send (wallet.send (ledger, key1.pub, 500, password));
    ASSERT_EQ (nullptr, send);
}

TEST (wallet, one_spend)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    mu_coin::wallet wallet (mu_coin::wallet_temp);
    wallet.insert (key1.pub, key1.prv, password);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub);
    mu_coin::block_hash latest1;
    store.latest_get (key1.address, latest1);
    mu_coin::keypair key2;
    auto send (wallet.send (ledger, key2.pub, 499, password));
    ASSERT_NE (nullptr, send);
    ASSERT_EQ (1, send->inputs.size ());
    ASSERT_EQ (1, send->outputs.size ());
    ASSERT_EQ (key1.address ^ latest1, send->inputs [0].previous);
    ASSERT_EQ (0, send->inputs [0].coins.coins ());
    ASSERT_FALSE (mu_coin::validate_message (send->hash (), send->signatures [0], key1.pub));
    ASSERT_EQ (key2.address, send->outputs [0].destination);
    ASSERT_EQ (499, send->outputs [0].coins.coins ());
}

TEST (wallet, two_spend)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::uint256_union password;
    mu_coin::wallet wallet (mu_coin::wallet_temp);
    wallet.insert (key1.pub, key1.prv, password);
    wallet.insert (key2.pub, key2.prv, password);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 100);
    mu_coin::block_hash hash1;
    ASSERT_FALSE (store.latest_get (key1.address, hash1));
    store.genesis_put (key2.pub, 400);
    mu_coin::block_hash hash2;
    ASSERT_FALSE (store.latest_get (key2.address, hash2));
    mu_coin::keypair key3;
    auto send (wallet.send (ledger, key3.pub, 499, password));
    ASSERT_NE (nullptr, send);
    ASSERT_EQ (2, send->inputs.size ());
    ASSERT_EQ (1, send->outputs.size ());
    ASSERT_EQ (key1.address ^ hash1, send->inputs [0].previous);
    ASSERT_EQ (0, send->inputs [0].coins.coins ());
    ASSERT_FALSE (mu_coin::validate_message (send->hash (), send->signatures [0], key1.pub));
    ASSERT_EQ (key2.address ^ hash2, send->inputs [1].previous);
    ASSERT_EQ (0, send->inputs [1].coins.coins ());
    ASSERT_FALSE (mu_coin::validate_message (send->hash (), send->signatures [1], key2.pub));
    ASSERT_EQ (key3.address, send->outputs [0].destination);
    ASSERT_EQ (499, send->outputs [0].coins.coins ());
}

TEST (wallet, partial_spend)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    mu_coin::wallet wallet (mu_coin::wallet_temp);
    wallet.insert (key1.pub, key1.prv, password);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 800);
    mu_coin::block_hash latest1;
    ASSERT_FALSE (store.latest_get (key1.address, latest1));
    mu_coin::keypair key2;
    auto send (wallet.send (ledger, key2.pub, 499, password));
    ASSERT_NE (nullptr, send);
    ASSERT_EQ (1, send->inputs.size ());
    ASSERT_EQ (1, send->outputs.size ());
    ASSERT_EQ (key1.address ^ latest1, send->inputs [0].previous);
    ASSERT_EQ (300, send->inputs [0].coins.coins ());
    ASSERT_FALSE (mu_coin::validate_message(send->hash (), send->signatures [0], key1.pub));
    ASSERT_EQ (key2.address, send->outputs [0].destination);
    ASSERT_EQ (499, send->outputs [0].coins.coins ());
}

TEST (wallet, spend_no_previous)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    mu_coin::wallet wallet (mu_coin::wallet_temp);
    for (auto i (0); i < 50; ++i)
    {
        mu_coin::keypair key;
        wallet.insert (key.pub, key.prv, password);
    }
    wallet.insert (key1.pub, key1.prv, password);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 500);
    mu_coin::block_hash hash1;
    ASSERT_FALSE (store.latest_get (key1.address, hash1));
    for (auto i (0); i < 50; ++i)
    {
        mu_coin::keypair key;
        wallet.insert (key.pub, key.prv, password);
    }
    mu_coin::keypair key2;
    auto send (wallet.send (ledger, key2.pub, 499, password));
    ASSERT_NE (nullptr, send);
    ASSERT_EQ (1, send->inputs.size ());
    ASSERT_EQ (1, send->outputs.size ());
    ASSERT_EQ (key1.address ^ hash1, send->inputs [0].previous);
    ASSERT_EQ (0, send->inputs [0].coins.coins ());
    ASSERT_FALSE (mu_coin::validate_message (send->hash (), send->signatures [0], key1.pub));
    ASSERT_EQ (key2.address, send->outputs [0].destination);
    ASSERT_EQ (499, send->outputs [0].coins.coins ());
}