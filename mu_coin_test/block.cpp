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
    mu_coin::keypair key1;
    mu_coin::uint256_t thing;
    mu_coin::transaction_block block;
    mu_coin::entry entry (key1.pub, 0, 0);
    ASSERT_EQ (key1.pub, entry.key ());
    block.entries.push_back (entry);
    ASSERT_EQ (1, block.entries.size ());
    boost::multiprecision::uint256_t hash (block.hash ());
    block.entries [0].sign (key1.prv, hash);
    bool valid1 (block.entries [0].validate (hash));
    ASSERT_TRUE (valid1);
    block.entries [0].signature.bytes [32] ^= 0x1;
    bool valid2 (block.entries [0].validate (hash));
    ASSERT_FALSE (valid2);
}

TEST (transaction_block, predecessor_check)
{
    mu_coin::transaction_block block;
}

TEST (transaction_block, empty_serialize)
{
    mu_coin::transaction_block block1;
    mu_coin::byte_write_stream stream;
    block1.serialize (stream);
    mu_coin::byte_read_stream input (stream.data, stream.size);
    mu_coin::transaction_block block2;
    block2.deserialize (input);
    ASSERT_EQ (block1, block2);
}

TEST (transaction_block, serialize_one_entry)
{
    mu_coin::transaction_block block1;
    mu_coin::byte_write_stream stream;
    mu_coin::keypair key1;
    mu_coin::entry entry1 (key1.pub, 37, 43);
    block1.entries.push_back (entry1);
    block1.serialize (stream);
    mu_coin::byte_read_stream input (stream.data, stream.size);
    mu_coin::transaction_block block2;
    block2.deserialize (input);
    ASSERT_EQ (block1, block2);
}

TEST (transaction_block, serialize_one_unequeal)
{
    mu_coin::transaction_block block1;
    mu_coin::byte_write_stream stream;
    mu_coin::keypair key1;
    mu_coin::entry entry1 (key1.pub, 37, 43);
    block1.entries.push_back (entry1);
    block1.serialize (stream);
    stream.data [0] ^= 1;
    mu_coin::byte_read_stream input (stream.data, stream.size);
    mu_coin::transaction_block block2;
    block2.deserialize (input);
    ASSERT_FALSE (block1 == block2);
}

TEST (transaction_block, serialize_two_entries)
{
    mu_coin::transaction_block block1;
    mu_coin::byte_write_stream stream;
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::entry entry1 (key1.pub, 37, 43);
    block1.entries.push_back (entry1);
    mu_coin::entry entry2 (key2.pub, 7, 11);
    block1.entries.push_back (entry2);
    block1.serialize (stream);
    mu_coin::byte_read_stream input (stream.data, stream.size);
    mu_coin::transaction_block block2;
    block2.deserialize (input);
    ASSERT_EQ (block1, block2);
}