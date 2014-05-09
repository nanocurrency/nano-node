#include <gtest/gtest.h>
#include <mu_coin/block.hpp>

TEST (transaction_block, big_endian_union_constructor)
{
    boost::multiprecision::uint256_t value1 (1);
    mu_coin::uint256_union bytes1 (value1);
    ASSERT_EQ (1, bytes1.bytes [31]);
    boost::multiprecision::uint512_t value2 (1);
    mu_coin::uint512_union bytes2 (value2);
    ASSERT_EQ (1, bytes2.bytes [63]);
}

TEST (transaction_block, big_endian_union_function)
{
    mu_coin::uint256_union bytes1;
    bytes1.clear ();
    bytes1.bytes [31] = 1;
    ASSERT_EQ (boost::multiprecision::uint256_t (1), bytes1.number ());
    mu_coin::uint512_union bytes2;
    bytes2.clear ();
    bytes2.bytes [63] = 1;
    ASSERT_EQ (boost::multiprecision::uint512_t (1), bytes2.number ());
}

TEST (transaction_block, empty)
{
    mu_coin::transaction_block block;
    ASSERT_EQ (0, block.inputs.size ());
    ASSERT_EQ (0, block.outputs.size ());
    ASSERT_FALSE (block.balanced ());
    boost::multiprecision::uint256_t hash (block.hash ());
    std::string str (hash.convert_to <std::string> ());
    ASSERT_EQ (boost::multiprecision::uint256_t ("0xE3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"), hash);
    mu_coin::EC::PrivateKey key;
    key.Initialize (mu_coin::pool (), mu_coin::curve ());
    block.sign (key);
}