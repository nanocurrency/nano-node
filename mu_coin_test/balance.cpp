#include <gtest/gtest.h>
#include <mu_coin/balance.hpp>
#include <mu_coin/block.hpp>

TEST (balance, balance_memory)
{
    mu_coin::balance_memory balance;
    ASSERT_EQ (0, balance.coins ());
    ASSERT_EQ (0, balance.votes ());
}

TEST (balance, balance_add)
{
    mu_coin::balance_memory balance;
    mu_coin::block_memory block (100, 200);
    balance += block;
    ASSERT_EQ (100, balance.coins ());
    ASSERT_EQ (200, balance.votes ());
}

TEST (balance, balance_subtract)
{
    mu_coin::balance_memory balance;
    mu_coin::block_memory block1 (400, 500);
    balance += block1;
    mu_coin::block_memory block2 (200, 100);
    balance -= block2;
    ASSERT_EQ (200, balance.coins ());
    ASSERT_EQ (400, balance.votes ());
}