#include <gtest/gtest.h>
#include <mu_coin/mu_coin.hpp>

TEST (block_store, construction)
{
    mu_coin::block_store db (mu_coin::block_store_temp);
}

TEST (block_store, add_item)
{
    mu_coin::block_store db (mu_coin::block_store_temp);
    mu_coin::send_block block;
    mu_coin::uint256_union hash1 (block.hash ());
    auto latest1 (db.block_get (hash1));
    ASSERT_EQ (nullptr, latest1);
    db.block_put (hash1, block);
    auto latest2 (db.block_get (hash1));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_nonempty_block)
{
    mu_coin::block_store db (mu_coin::block_store_temp);
    mu_coin::keypair key1;
    mu_coin::send_block block;
    mu_coin::send_input entry1 (key1.pub, 0, (mu_coin::amount ()));
    block.inputs.push_back (entry1);
    block.signatures.push_back (mu_coin::uint512_union ());
    mu_coin::uint256_union hash1 (block.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, block.signatures.back ());
    auto latest1 (db.block_get (hash1));
    ASSERT_EQ (nullptr, latest1);
    db.block_put (hash1, block);
    auto latest2 (db.block_get (hash1));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_two_items)
{
    mu_coin::block_store db (mu_coin::block_store_temp);
    mu_coin::keypair key1;
    mu_coin::send_block block;
    mu_coin::send_input entry1 (key1.pub, 0, 1);
    block.inputs.push_back (entry1);
    mu_coin::uint256_union hash1 (block.hash ());
    block.signatures.push_back (mu_coin::uint512_union ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, block.signatures.back ());
    auto latest1 (db.block_get (hash1));
    ASSERT_EQ (nullptr, latest1);
    mu_coin::send_block block2;
    mu_coin::send_input entry2 (key1.pub, 1, 3);
    block2.inputs.push_back (entry2);
    mu_coin::uint256_union hash2 (block2.hash ());
    block2.signatures.push_back (mu_coin::uint512_union ());
    mu_coin::sign_message (key1.prv, key1.pub, hash2, block2.signatures.back ());
    auto latest2 (db.block_get (hash2));
    ASSERT_EQ (nullptr, latest2);
    db.block_put (hash1, block);
    db.block_put (hash2, block2);
    auto latest3 (db.block_get (hash1));
    ASSERT_NE (nullptr, latest3);
    ASSERT_EQ (block, *latest3);
    auto latest4 (db.block_get (hash2));
    ASSERT_NE (nullptr, latest4);
    ASSERT_EQ (block2, *latest4);
    ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
    mu_coin::block_store db (mu_coin::block_store_temp);
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::receive_block block;
    mu_coin::block_hash hash1 (block.hash ());
    auto latest1 (db.block_get (hash1));
    ASSERT_EQ (nullptr, latest1);
    db.block_put (hash1, block);
    auto latest2 (db.block_get (hash1));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_pending)
{
    mu_coin::block_store db (mu_coin::block_store_temp);
    mu_coin::keypair key1;
    mu_coin::block_hash hash1;
    auto pending1 (db.pending_get (key1.pub, hash1));
    ASSERT_TRUE (pending1);
    db.pending_put (key1.pub, hash1);
    auto pending2 (db.pending_get (key1.pub, hash1));
    ASSERT_FALSE (pending2);
    db.pending_del (key1.pub, hash1);
    auto pending3 (db.pending_get (key1.pub, hash1));
    ASSERT_TRUE (pending3);
}

TEST (block_store, add_genesis)
{
    mu_coin::block_store db (mu_coin::block_store_temp);
    mu_coin::keypair key1;
    db.genesis_put (key1.pub, 800);
    mu_coin::block_hash hash1;
    ASSERT_FALSE (db.latest_get (key1.pub, hash1));
    mu_coin::block_hash hash2;
    ASSERT_FALSE (db.identifier_get (key1.pub ^ hash1, hash2));
    ASSERT_EQ (hash1, hash2);
    auto block1 (db.block_get (hash1));
    ASSERT_NE (nullptr, block1);
    auto receive1 (dynamic_cast <mu_coin::receive_block *> (block1.get ()));
    ASSERT_NE (nullptr, receive1);
    ASSERT_EQ (key1.pub, receive1->previous);
    auto block2 (db.block_get (receive1->source));
    auto send1 (dynamic_cast <mu_coin::send_block *> (block2.get ()));
    ASSERT_NE (nullptr, send1);
    ASSERT_EQ (0, send1->inputs.size ());
    ASSERT_EQ (0, send1->signatures.size ());
    ASSERT_EQ (1, send1->outputs.size ());
    ASSERT_EQ (key1.pub, send1->outputs [0].destination);
}