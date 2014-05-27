#include <gtest/gtest.h>
#include <mu_coin/mu_coin.hpp>
#include <cryptopp/filters.h>
#include <cryptopp/randpool.h>

TEST (ledger, empty)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::address address;
    ASSERT_FALSE (ledger.has_balance (address));
}

TEST (ledger, genesis_balance)
{
    mu_coin::keypair key1;
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry (key1.pub, max, 0);
    genesis.entries.push_back (entry);
    genesis.entries [0].sign (key1.prv, genesis.hash ());
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.insert_block (entry.id, genesis);
}

TEST (address, two_addresses)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    ASSERT_FALSE (key1.pub == key2.pub);
    mu_coin::point_encoding point1 (key1.pub);
    mu_coin::address addr1 (point1.key ());
    mu_coin::point_encoding point2 (key2.pub);
    mu_coin::address addr2 (point2.key ());
    ASSERT_FALSE (addr1 == addr2);
}

TEST (point_encoding, validation_fail)
{
    mu_coin::point_encoding encoding;
    ASSERT_TRUE (encoding.validate ());
}

TEST (point_encoding, validation_succeed)
{
    mu_coin::keypair key1;
    mu_coin::point_encoding encoding (key1.pub);
    ASSERT_FALSE (encoding.validate ());
}

TEST (ledger, simple_spend)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (key1.pub, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (key1.prv, genesis.hash ());
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.insert_block (entry1.id, genesis);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (key1.pub, 0, 1);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (key2.pub, max - 1, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (key1.prv, spend.hash ());
    spend.entries [1].sign (key2.prv, spend.hash ());
    auto error (ledger.process (spend));
    ASSERT_FALSE (error);
    auto block1 (store.latest (entry2.id.address));
    auto block2 (store.latest (entry3.id.address));
    ASSERT_NE (nullptr, block1);
    ASSERT_NE (nullptr, block2);
    ASSERT_EQ (*block1, *block2);
}

TEST (ledger, fail_out_of_sequence)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (key1.pub, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (key1.prv, genesis.hash ());
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.insert_block (entry1.id, genesis);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (key1.pub, 0, 2);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (key2.pub, max - 1, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (key1.prv, spend.hash ());
    spend.entries [1].sign (key2.prv, spend.hash ());
    auto error (ledger.process (spend));
    ASSERT_TRUE (error);
}

TEST (ledger, fail_fee_too_high)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (key1.pub, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (key1.prv, genesis.hash ());
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.insert_block (entry1.id, genesis);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (key1.pub, 0, 1);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (key2.pub, max - 2, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (key1.prv, spend.hash ());
    spend.entries [1].sign (key2.prv, spend.hash ());
    auto error (ledger.process (spend));
    ASSERT_TRUE (error);
}

TEST (ledger, fail_fee_too_low)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (key1.pub, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (key1.prv, genesis.hash ());
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.insert_block (entry1.id, genesis);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (key1.pub, 0, 1);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (key2.pub, max - 0, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (key1.prv, spend.hash ());
    spend.entries [1].sign (key2.prv, spend.hash ());
    auto error (ledger.process (spend));
    ASSERT_TRUE (error);
}

TEST (ledger, fail_bad_signature)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (key1.pub, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (key1.prv, genesis.hash ());
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.insert_block (entry1.id, genesis);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (key1.pub, 0, 1);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (key2.pub, max - 1, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (key1.prv, spend.hash ());
    spend.entries [0].signature.bytes [32] ^= 1;
    spend.entries [1].sign (key2.prv, spend.hash ());
    auto error (ledger.process (spend));
    ASSERT_TRUE (error);
}

TEST (ledger, join_spend)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::keypair key3;
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (key1.pub, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (key1.prv, genesis.hash ());
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.insert_block (entry1.id, genesis);
    mu_coin::transaction_block spend1;
    mu_coin::entry entry2 (key1.pub, 0, 1);
    spend1.entries.push_back (entry2);
    mu_coin::entry entry3 (key2.pub, max - 11, 0);
    spend1.entries.push_back (entry3);
    mu_coin::entry entry4 (key3.pub, 10, 0);
    spend1.entries.push_back (entry4);
    spend1.entries [0].sign (key1.prv, spend1.hash ());
    spend1.entries [1].sign (key2.prv, spend1.hash ());
    spend1.entries [2].sign (key3.prv, spend1.hash ());
    auto error1 (ledger.process (spend1));
    ASSERT_FALSE (error1);
    mu_coin::transaction_block spend2;
    mu_coin::entry entry5 (key1.pub, max - 2, 2);
    spend2.entries.push_back (entry5);
    mu_coin::entry entry6 (key2.pub, 0, 1);
    spend2.entries.push_back (entry6);
    mu_coin::entry entry7 (key3.pub, 0, 1);
    spend2.entries.push_back (entry7);
    spend2.entries [0].sign (key1.prv, spend2.hash ());
    spend2.entries [1].sign (key2.prv, spend2.hash ());
    spend2.entries [2].sign (key3.prv, spend2.hash ());
    auto error2 (ledger.process (spend2));
    ASSERT_FALSE (error2);
}

TEST (uint256_union, key_encryption)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union secret_key;
    secret_key.bytes.fill (0);
    mu_coin::point_encoding encoded (key1.pub);
    mu_coin::uint256_union encrypted (key1.prv, secret_key, encoded.iv ());
    mu_coin::EC::PrivateKey key4 (encrypted.key (secret_key, encoded.iv ()));
    ASSERT_EQ (key1.prv.GetPrivateExponent (), key4.GetPrivateExponent());
    mu_coin::EC::PublicKey pub;
    key4.MakePublicKey (pub);
    ASSERT_EQ (key1.pub, pub);
}

TEST (ledger, process_send)
{
    mu_coin::keypair key1;
    mu_coin::transaction_block genesis;
    mu_coin::entry entry1 (key1.pub, 100, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (key1.prv, genesis.hash ());
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.insert_block (entry1.id, genesis);
    mu_coin::send_block send;
    mu_coin::send_input entry2 (key1.pub, 49, 1);
    mu_coin::keypair key2;
    send.inputs.push_back (entry2);
    mu_coin::send_output entry3 (key2.pub, 50);
    send.outputs.push_back (entry3);
    send.inputs [0].sign (key1.prv, send.hash ());
    auto error1 (ledger.process (send));
    ASSERT_FALSE (error1);
    mu_coin::receive_block receive;
    receive.source = entry2.source;
    receive.output = mu_coin::block_id (key2.pub, 0);
    receive.coins = mu_coin::uint256_t (50);
    receive.sign (key2.prv, receive.hash ());
    auto error2 (ledger.process (receive));
    ASSERT_FALSE (error2);
}