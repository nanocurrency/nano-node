#include <gtest/gtest.h>
#include <mu_coin_store/db.hpp>

TEST (block_store_db, construction)
{
    mu_coin_store::block_store_db db (mu_coin_store::block_store_db_temp);
}

TEST (block_store_db, empty_account)
{
    mu_coin_store::block_store_db db (mu_coin_store::block_store_db_temp);
    mu_coin::keypair key1;
    mu_coin::address address;
    auto latest (db.latest (address));
    ASSERT_EQ (nullptr, latest);
}

TEST (block_store_db, add_item)
{
    mu_coin_store::block_store_db db (mu_coin_store::block_store_db_temp);
    mu_coin::keypair key1;
    mu_coin::block_id block_id (key1.pub, 0);
    auto latest1 (db.latest (block_id.address));
    ASSERT_EQ (nullptr, latest1);
    mu_coin::transaction_block block;
    db.insert_block (block_id, block);
    auto latest2 (db.latest (block_id.address));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
}

TEST (block_store_db, add_nonempty_block)
{
    mu_coin_store::block_store_db db (mu_coin_store::block_store_db_temp);
    mu_coin::keypair key1;
    mu_coin::block_id block_id (key1.pub, 0);
    auto latest1 (db.latest (block_id.address));
    ASSERT_EQ (nullptr, latest1);
    mu_coin::transaction_block block;
    mu_coin::entry entry1 (key1.pub, 100, 0);
    block.entries.push_back (entry1);
    db.insert_block (block_id, block);
    auto latest2 (db.latest (block_id.address));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
}

TEST (block_store_db, add_two_items)
{
    mu_coin_store::block_store_db db (mu_coin_store::block_store_db_temp);
    mu_coin::keypair key1;
    mu_coin::block_id block_id (key1.pub, 0);
    auto latest1 (db.latest (block_id.address));
    ASSERT_EQ (nullptr, latest1);
    mu_coin::transaction_block block;
    mu_coin::entry entry1 (key1.pub, 100, 0);
    block.entries.push_back (entry1);
    db.insert_block (block_id, block);
    auto latest2 (db.latest (block_id.address));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
    mu_coin::block_id block_id2 (key1.pub, 1);
    mu_coin::transaction_block block2;
    mu_coin::entry entry2 (key1.pub, 200, 1);
    db.insert_block (block_id2, block2);
    auto latest3 (db.latest (block_id.address));
    ASSERT_NE (nullptr, latest3);
    ASSERT_EQ (block2, *latest3);
    ASSERT_FALSE (*latest2 == *latest3);
}

TEST (block_store_db, add_send)
{
    mu_coin_store::block_store_db db (mu_coin_store::block_store_db_temp);
    mu_coin::keypair key1;
    mu_coin::block_id block_id (key1.pub, 0);
    auto latest1 (db.latest (block_id.address));
    ASSERT_EQ (nullptr, latest1);
    mu_coin::send_block block;
    mu_coin::send_input entry1 (key1.pub, 100, 0);
    block.inputs.push_back (entry1);
    db.insert_block (block_id, block);
    auto latest2 (db.latest (block_id.address));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
}

TEST (block_store_db, add_receive)
{
    mu_coin_store::block_store_db db (mu_coin_store::block_store_db_temp);
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::block_id block_id1 (key1.pub, 0);
    mu_coin::block_id block_id2 (key2.pub, 0);
    auto latest1 (db.latest (block_id1.address));
    ASSERT_EQ (nullptr, latest1);
    mu_coin::receive_block block;
    block.output = block_id1;
    block.source = block_id2;
    db.insert_block (block_id1, block);
    auto latest2 (db.latest (block_id1.address));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
}

TEST (block_store_db, add_send_half)
{
    mu_coin_store::block_store_db db (mu_coin_store::block_store_db_temp);
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::block_id block_id (key1.pub, 0);
    mu_coin::address address1 (key2.pub);
    auto send1 (db.send (address1, block_id));
    ASSERT_EQ (nullptr, send1);
    mu_coin::send_block block;
    mu_coin::send_input entry1 (key1.pub, 100, 0);
    block.inputs.push_back (entry1);
    mu_coin::send_output entry2 (key2.pub, 50);
    block.outputs.push_back (entry2);
    db.insert_send (address1, block);
    auto send2 (db.send (address1, block_id));
    ASSERT_NE (nullptr, send2);
    ASSERT_EQ (block, *send2);
    db.clear (address1, block_id);
    auto send3 (db.send (address1, block_id));
    ASSERT_EQ (nullptr, send3);
}