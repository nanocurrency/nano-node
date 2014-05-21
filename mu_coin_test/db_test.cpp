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