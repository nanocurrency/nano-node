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
    ASSERT_FALSE (valid1);
    block.entries [0].signature.bytes [32] ^= 0x1;
    bool valid2 (block.entries [0].validate (hash));
    ASSERT_TRUE (valid2);
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

TEST (send_block, empty_send_serialize)
{
    mu_coin::send_block block1;
    mu_coin::byte_write_stream stream1;
    block1.serialize (stream1);
    mu_coin::byte_read_stream stream2 (stream1.data, stream1.size);
    mu_coin::send_block block2;
    block2.deserialize (stream2);
    ASSERT_EQ (block1, block2);
}

TEST (send_block, two_entry_send_serialize)
{
    mu_coin::send_block block1;
    mu_coin::byte_write_stream stream1;
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::send_input entry1 (key1.pub, 37, 43);
    block1.inputs.push_back (entry1);
    mu_coin::send_input entry2 (key1.pub, 11, 17);
    block1.inputs.push_back (entry2);
    block1.inputs [0].sign (key1.prv, block1.hash ());
    block1.inputs [1].sign (key1.prv, block1.hash ());
    mu_coin::send_output entry3 (key2.pub, 23);
    block1.outputs.push_back (entry3);
    block1.serialize (stream1);
    mu_coin::byte_read_stream stream2 (stream1.data, stream1.size);
    mu_coin::send_block block2;
    block2.deserialize (stream2);
    ASSERT_EQ (block1, block2);
}

TEST (send_block, receive_serialize)
{
    mu_coin::receive_block block1;
    mu_coin::keypair key1;
    mu_coin::byte_write_stream stream1;
    block1.source = mu_coin::block_id (key1.pub, 17);
    block1.output = mu_coin::block_id (key1.pub, 23);
    block1.serialize (stream1);
    mu_coin::byte_read_stream stream2 (stream1.data, stream1.size);
    mu_coin::receive_block block2;
    auto error (block2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (block1, block2);
}

TEST (send_block, balance)
{
    mu_coin::send_block block;
    mu_coin::keypair key1;
    mu_coin::address address1 (key1.pub);
    mu_coin::send_input entry1 (key1.pub, 42, 37);
    block.inputs.push_back (entry1);
    mu_coin::keypair key2;
    mu_coin::address address2 (key2.pub);
    mu_coin::send_output entry2 (key2.pub, 17);
    mu_coin::uint256_t coins;
    uint16_t sequence;
    auto error1 (block.balance (address1, coins, sequence));
    ASSERT_FALSE (error1);
    auto error2 (block.balance (address2, coins, sequence));
    ASSERT_TRUE (error2);
    ASSERT_EQ (42, coins);
    ASSERT_EQ (37, sequence);
    mu_coin::keypair key3;
    mu_coin::address address3 (key3.pub);
    auto error3 (block.balance (address3, coins, sequence));
    ASSERT_TRUE (error3);
}

TEST (receive_block, balance)
{
    mu_coin::receive_block block;
    mu_coin::keypair key1;
    block.output.address = key1.pub;
    block.coins = mu_coin::uint256_t (42);
    block.output.sequence = 97;
    mu_coin::uint256_t coins;
    uint16_t sequence;
    mu_coin::address address1 (key1.pub);
    auto error1 (block.balance (address1, coins, sequence));
    ASSERT_FALSE (error1);
    ASSERT_EQ (mu_coin::uint256_t (42), coins);
    ASSERT_EQ (97, sequence);
    mu_coin::keypair key2;
    mu_coin::address address2 (key2.pub);
    auto error2 (block.balance (address2, coins, sequence));
    ASSERT_TRUE (error2);
}

TEST (uint256_union, parse_zero)
{
    mu_coin::uint256_union input (mu_coin::uint256_t (0));
    std::string text;
    input.encode_hex (text);
    mu_coin::uint256_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint256_union, parse_zero_short)
{
    std::string text ("0");
    mu_coin::uint256_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint256_union, parse_one)
{
    mu_coin::uint256_union input (mu_coin::uint256_t (1));
    std::string text;
    input.encode_hex (text);
    mu_coin::uint256_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_EQ (1, output.number ());
}

TEST (uint256_union, parse_error_symbol)
{
    mu_coin::uint256_union input (mu_coin::uint256_t (1000));
    std::string text;
    input.encode_hex (text);
    text [5] = '!';
    mu_coin::uint256_union output;
    auto error (output.decode_hex (text));
    ASSERT_TRUE (error);
}

TEST (uint256_union, max)
{
    mu_coin::uint256_union input (std::numeric_limits <mu_coin::uint256_t>::max ());
    std::string text;
    input.encode_hex (text);
    mu_coin::uint256_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_EQ (mu_coin::uint256_t ("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), output.number ());
}

TEST (uint256_union, parse_error_overflow)
{
    mu_coin::uint256_union input (std::numeric_limits <mu_coin::uint256_t>::max ());
    std::string text;
    input.encode_hex (text);
    text.push_back (0);
    mu_coin::uint256_union output;
    auto error (output.decode_hex (text));
    ASSERT_TRUE (error);
}

TEST (uint512_union, parse_zero)
{
    mu_coin::uint512_union input (mu_coin::uint512_t (0));
    std::string text;
    input.encode_hex (text);
    mu_coin::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint512_union, parse_zero_short)
{
    std::string text ("0");
    mu_coin::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint512_union, parse_one)
{
    mu_coin::uint512_union input (mu_coin::uint512_t (1));
    std::string text;
    input.encode_hex (text);
    mu_coin::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_EQ (1, output.number ());
}

TEST (uint512_union, parse_error_symbol)
{
    mu_coin::uint512_union input (mu_coin::uint512_t (1000));
    std::string text;
    input.encode_hex (text);
    text [5] = '!';
    mu_coin::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_TRUE (error);
}

TEST (uint512_union, max)
{
    mu_coin::uint512_union input (std::numeric_limits <mu_coin::uint512_t>::max ());
    std::string text;
    input.encode_hex (text);
    mu_coin::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_EQ (mu_coin::uint512_t ("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), output.number ());
}

TEST (uint512_union, parse_error_overflow)
{
    mu_coin::uint512_union input (std::numeric_limits <mu_coin::uint512_t>::max ());
    std::string text;
    input.encode_hex (text);
    text.push_back (0);
    mu_coin::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_TRUE (error);
}

TEST (point_encoding, parse)
{
    mu_coin::keypair key1;
    mu_coin::point_encoding point1 (key1.pub);
    std::string text;
    point1.encode_hex (text);
    mu_coin::point_encoding point2;
    auto error1 (point2.decode_hex (text));
    ASSERT_FALSE (error1);
    auto error2 (point2.validate ());
    ASSERT_FALSE (error2);
    ASSERT_EQ (point1.key (), point2.key ());
}