#include <gtest/gtest.h>
#include <mu_coin/ledger.hpp>
#include <mu_coin/address.hpp>
#include <mu_coin/block.hpp>

TEST (ledger, empty)
{
    mu_coin::ledger_memory ledger;
    mu_coin::balance & balance (ledger.balance (mu_coin::address (0)));
    ASSERT_EQ (0, balance.coins ());
    ASSERT_EQ (0, balance.votes ());
}

TEST (ledger, separate_empty)
{
    mu_coin::ledger_memory ledger;
    mu_coin::balance & balance1 (ledger.balance (mu_coin::address (0)));
    ASSERT_EQ (0, balance1.coins ());
    ASSERT_EQ (0, balance1.votes ());
    mu_coin::balance & balance2 (ledger.balance (mu_coin::address (0)));
    ASSERT_EQ (0, balance2.coins ());
    ASSERT_EQ (0, balance2.votes ());
    mu_coin::block_memory block1 (100, 200);
    balance1 += block1;
    ASSERT_EQ (100, balance1.coins ());
    ASSERT_EQ (200, balance1.votes ());
    ASSERT_EQ (0, balance2.coins ());
    ASSERT_EQ (0, balance2.votes ());
}