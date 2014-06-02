#include <gtest/gtest.h>
#include <mu_coin/mu_coin.hpp>
#include <cryptopp/filters.h>
#include <cryptopp/randpool.h>

TEST (ledger, empty)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::address address;
    auto balance (ledger.balance (address));
    ASSERT_TRUE (balance.coins ().is_zero ());
}

TEST (ledger, genesis_balance)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 500);
    auto balance (ledger.balance (key1.address));
    ASSERT_EQ (500, balance.coins ());
}

TEST (address, two_addresses)
{
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    ASSERT_FALSE (key1.pub == key2.pub);
    bool y1;
    mu_coin::address address1 (key1.pub, y1);
    ASSERT_EQ (key1.address, address1);
    ASSERT_EQ (key1.y, y1);
    bool y2;
    mu_coin::address address2 (key2.pub, y2);
    ASSERT_EQ (key2.address, address2);
    ASSERT_EQ (key2.y, y2);
    ASSERT_FALSE (address1 == address2);
}

TEST (point_encoding, validation_fail)
{
    mu_coin::uint256_union encoding;
    encoding.bytes.fill (0xff);
    bool y (false);
    ASSERT_TRUE (encoding.validate (y));
}

TEST (point_encoding, validation_succeed)
{
    mu_coin::keypair key1;
    bool y;
    mu_coin::uint256_union encoding (key1.pub, y);
    ASSERT_FALSE (encoding.validate (y));
}

TEST (uint256_union, key_encryption)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union secret_key;
    secret_key.bytes.fill (0);
    bool y;
    mu_coin::uint256_union encoded (key1.pub, y);
    mu_coin::uint256_union encrypted (key1.prv, secret_key, encoded.owords [0]);
    mu_coin::EC::PrivateKey key4 (encrypted.prv (secret_key, encoded.owords [0]));
    ASSERT_EQ (key1.prv.GetPrivateExponent (), key4.GetPrivateExponent());
    mu_coin::EC::PublicKey pub;
    key4.MakePublicKey (pub);
    ASSERT_EQ (key1.pub, pub);
}

TEST (ledger, process_send)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 100);
    mu_coin::block_hash block1;
    ASSERT_FALSE (store.latest_get (key1.address, block1));
    mu_coin::send_block send;
    mu_coin::send_input entry2 (key1.pub, block1, 49);
    mu_coin::keypair key2;
    send.inputs.push_back (entry2);
    mu_coin::send_output entry3 (key2.pub, 50);
    send.outputs.push_back (entry3);
    mu_coin::block_hash hash1 (send.hash ());
    send.signatures.push_back (mu_coin::uint512_union ());
    mu_coin::sign_message (key1.prv, hash1, send.signatures.back ());
    auto error1 (ledger.process (send));
    ASSERT_FALSE (error1);
    bool y2;
    mu_coin::address address2 (key2.pub, y2);
    mu_coin::receive_block receive;
    receive.source = hash1;
    receive.previous = address2;
    mu_coin::block_hash hash2 (receive.hash ());
    receive.sign (key2.prv, hash2);
    auto error2 (ledger.process (receive));
    ASSERT_FALSE (error2);
    mu_coin::block_hash hash3;
    auto latest1 (store.latest_get (key1.address, hash3));
    ASSERT_FALSE (latest1);
    auto latest2 (store.block_get (hash3));
    auto latest3 (dynamic_cast <mu_coin::send_block *> (latest2.get ()));
    ASSERT_NE (nullptr, latest3);
    ASSERT_EQ (send, *latest3);
}