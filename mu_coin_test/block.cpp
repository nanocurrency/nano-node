#include <gtest/gtest.h>
#include <mu_coin/block.hpp>

TEST (block, empty)
{
    mu_coin::block block;
    ASSERT_EQ (0, block.inputs.size ());
    ASSERT_EQ (0, block.outputs.size ());
    ASSERT_TRUE (block.balanced ());
    boost::multiprecision::uint256_t hash (block.hash ());
    std::string str (hash.convert_to <std::string> ());
    ASSERT_EQ (boost::multiprecision::uint256_t ("0xE3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"), hash);
}