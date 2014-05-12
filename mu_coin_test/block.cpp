#include <gtest/gtest.h>
#include <mu_coin/mu_coin.hpp>

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
    mu_coin::EC::PrivateKey prv;
    prv.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub;
    prv.MakePublicKey (pub);
    mu_coin::uint256_t thing;
    mu_coin::uint256_union address_number (pub);
    mu_coin::address address (address_number.number ());
    mu_coin::transaction_block block;
    block.entries.push_back (mu_coin::entry (0, 0, 0));
    ASSERT_EQ (1, block.entries.size ());
    boost::multiprecision::uint256_t hash (block.hash ());
    block.entries [0].sign (prv, hash);
    std::string str (hash.convert_to <std::string> ());
    ASSERT_EQ (boost::multiprecision::uint256_t ("0xEFBB03B7A7F6FD3C29391D4D0281E1830A85CAADD831C3F04716FACA4107A42E"), hash);
    bool valid1 (block.entries [0].validate (pub, hash));
    ASSERT_TRUE (valid1);
    block.entries [0].signature.bytes [32] ^= 0x1;
    bool valid2 (block.entries [0].validate (pub, hash));
    ASSERT_FALSE (valid2);
}

TEST (transaction_block, predecessor_check)
{
    mu_coin::transaction_block block;
}