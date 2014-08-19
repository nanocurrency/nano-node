#include <gtest/gtest.h>

#include <mu_coin/mu_coin.hpp>

TEST (wallet, no_key)
{
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    mu_coin::keypair key1;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    mu_coin::private_key prv1;
    ASSERT_TRUE (wallet.fetch (key1.pub, secret, prv1));
}

TEST (wallet, retrieval)
{
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    mu_coin::keypair key1;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (key1.prv, secret);
    mu_coin::private_key prv1;
    ASSERT_FALSE (wallet.fetch (key1.pub, secret, prv1));
    ASSERT_EQ (key1.prv, prv1);
    secret.bytes [16] ^= 1;
    mu_coin::private_key prv2;
    ASSERT_TRUE (wallet.fetch (key1.pub, secret, prv2));
}

TEST (wallet, empty_iteration)
{
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    auto i (wallet.begin ());
    auto j (wallet.end ());
    ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    mu_coin::keypair key1;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (key1.prv, secret);
    for (auto i (wallet.begin ()), j (wallet.end ()); i != j; ++i)
    {
        ASSERT_EQ (key1.pub, i->first);
        ASSERT_EQ (key1.prv, i->second.prv (secret, i->first.owords [0]));
    }
}

TEST (wallet, two_item_iteration)
{
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::uint256_union secret;
    secret.bytes.fill (0);
    wallet.insert (key1.prv, secret);
    wallet.insert (key2.prv, secret);
    std::vector <mu_coin::public_key> keys1;
    std::vector <mu_coin::private_key> keys2;
    for (auto i (wallet.begin ()), j (wallet.end ()); i != j; ++i)
    {
        keys1.push_back (i->first);
        keys2.push_back (i->second.prv (secret, i->first.owords [0]));
    }
    ASSERT_EQ (2, keys1.size ());
    ASSERT_EQ (2, keys2.size ());
    ASSERT_NE (keys1.end (), std::find (keys1.begin (), keys1.end (), key1.pub));
    ASSERT_NE (keys2.end (), std::find (keys2.begin (), keys2.end (), key1.prv));
    ASSERT_NE (keys1.end (), std::find (keys1.begin (), keys1.end (), key2.pub));
    ASSERT_NE (keys2.end (), std::find (keys2.begin (), keys2.end (), key2.prv));
}

TEST (wallet, insufficient_spend)
{
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    std::vector <std::unique_ptr <mu_coin::send_block>> blocks;
    ASSERT_TRUE (wallet.generate_send (ledger, key1.pub, 500, password, blocks));
}

TEST (wallet, one_spend)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    wallet.insert (key1.pub, key1.prv, password);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis (key1.pub, 500);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    store.latest_get (key1.pub, frontier1);
    mu_coin::keypair key2;
    std::vector <std::unique_ptr <mu_coin::send_block>> blocks;
    ASSERT_FALSE (wallet.generate_send (ledger, key2.pub, 500, password, blocks));
    ASSERT_EQ (1, blocks.size ());
    auto & send (*blocks [0]);
    ASSERT_EQ (frontier1.hash, send.hashables.previous);
    ASSERT_EQ (0, send.hashables.balance.number ());
    ASSERT_FALSE (mu_coin::validate_message (key1.pub, send.hash (), send.signature));
    ASSERT_EQ (key2.pub, send.hashables.destination);
}

TEST (wallet, two_spend)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::uint256_union password;
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    wallet.insert (key1.pub, key1.prv, password);
    wallet.insert (key2.pub, key2.prv, password);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis1 (key1.pub, 100);
    genesis1.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::genesis genesis2 (key2.pub, 400);
    genesis2.initialize (store);
    mu_coin::frontier frontier2;
    ASSERT_FALSE (store.latest_get (key2.pub, frontier2));
    mu_coin::keypair key3;
    std::vector <std::unique_ptr <mu_coin::send_block>> blocks;
    ASSERT_FALSE (wallet.generate_send (ledger, key3.pub, 500, password, blocks));
    ASSERT_EQ (2, blocks.size ());
    ASSERT_TRUE (std::all_of (blocks.begin (), blocks.end (), [] (std::unique_ptr <mu_coin::send_block> const & block) {return block->hashables.balance == 0;}));
    ASSERT_TRUE (std::all_of (blocks.begin (), blocks.end (), [key3] (std::unique_ptr <mu_coin::send_block> const & block) {return block->hashables.destination == key3.pub;}));
    ASSERT_TRUE (std::any_of (blocks.begin (), blocks.end (), [key1] (std::unique_ptr <mu_coin::send_block> const & block) {return !mu_coin::validate_message(key1.pub, block->hash (), block->signature);}));
    ASSERT_TRUE (std::any_of (blocks.begin (), blocks.end (), [key2] (std::unique_ptr <mu_coin::send_block> const & block) {return !mu_coin::validate_message(key2.pub, block->hash (), block->signature);}));
}

TEST (wallet, partial_spend)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    wallet.insert (key1.pub, key1.prv, password);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis (key1.pub, 800);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::keypair key2;
    std::vector <std::unique_ptr <mu_coin::send_block>> blocks;
    ASSERT_FALSE (wallet.generate_send (ledger, key2.pub, 500, password, blocks));
    ASSERT_EQ (1, blocks.size ());
    ASSERT_EQ (frontier1.hash, blocks [0]->hashables.previous);
    ASSERT_EQ (300, blocks [0]->hashables.balance.number ());
    ASSERT_FALSE (mu_coin::validate_message (key1.pub, blocks [0]->hash (), blocks [0]->signature));
    ASSERT_EQ (key2.pub, blocks [0]->hashables.destination);
}

TEST (wallet, spend_no_previous)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union password;
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    for (auto i (0); i < 50; ++i)
    {
        mu_coin::keypair key;
        wallet.insert (key.pub, key.prv, password);
    }
    wallet.insert (key1.pub, key1.prv, password);
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis (key1.pub, 500);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    for (auto i (0); i < 50; ++i)
    {
        mu_coin::keypair key;
        wallet.insert (key.pub, key.prv, password);
    }
    mu_coin::keypair key2;
    std::vector <std::unique_ptr <mu_coin::send_block>> blocks;
    ASSERT_FALSE (wallet.generate_send (ledger, key2.pub, 500, password, blocks));
    ASSERT_EQ (1, blocks.size ());
    ASSERT_EQ (frontier1.hash, blocks [0]->hashables.previous);
    ASSERT_EQ (0, blocks [0]->hashables.balance.number ());
    ASSERT_FALSE (mu_coin::validate_message (key1.pub, blocks [0]->hash (), blocks [0]->signature));
    ASSERT_EQ (key2.pub, blocks [0]->hashables.destination);
}

TEST (wallet, find_none)
{
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    mu_coin::uint256_union account;
    ASSERT_EQ (wallet.end (), wallet.find (account));
}

TEST (wallet, find_existing)
{
    mu_coin::wallet wallet (0, mu_coin::wallet_temp);
    mu_coin::keypair key1;
    wallet.insert (key1.pub, key1.prv, wallet.password);
    auto existing (wallet.find (key1.pub));
    ASSERT_NE (wallet.end (), existing);
    ++existing;
    ASSERT_EQ (wallet.end (), existing);
}

TEST (base58, encode_zero)
{
    mu_coin::uint256_union number0 (0);
    std::string str0;
    number0.encode_base58check (str0);
    ASSERT_EQ (50, str0.size ());
    mu_coin::uint256_union number1;
    ASSERT_FALSE (number1.decode_base58check (str0));
    ASSERT_EQ (number0, number1);
}

TEST (base58, encode_all)
{
    mu_coin::uint256_union number0;
    number0.decode_hex ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    std::string str0;
    number0.encode_base58check (str0);
    ASSERT_EQ (50, str0.size ());
    mu_coin::uint256_union number1;
    ASSERT_FALSE (number1.decode_base58check (str0));
    ASSERT_EQ (number0, number1);
}

TEST (base58, encode_fail)
{
    mu_coin::uint256_union number0 (0);
    std::string str0;
    number0.encode_base58check (str0);
    str0 [16] ^= 1;
    mu_coin::uint256_union number1;
    ASSERT_TRUE (number1.decode_base58check (str0));
}