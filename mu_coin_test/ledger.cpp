#include <gtest/gtest.h>
#include <mu_coin/ledger.hpp>
#include <mu_coin/address.hpp>
#include <mu_coin/block.hpp>

TEST (ledger, empty)
{
    mu_coin::ledger ledger;
    ASSERT_FALSE (ledger.has_balance (mu_coin::address (0)));
}