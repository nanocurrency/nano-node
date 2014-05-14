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