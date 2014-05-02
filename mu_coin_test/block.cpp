#include <gtest/gtest.h>
#include <mu_coin/block.hpp>

TEST (block, block_memory)
{
    mu_coin::block_memory block;
    ASSERT_EQ (0, block.coins ());
    ASSERT_EQ (0, block.votes ());
}