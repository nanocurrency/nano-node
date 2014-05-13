#include <gtest/gtest.h>
#include <mu_coin_store/db.hpp>

TEST (block_store_db, construction)
{
    mu_coin_store::block_store_db db (mu_coin_store::block_store_db_temp);
}