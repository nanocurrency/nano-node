#include <gtest/gtest.h>
#include <mu_coin/mu_coin.hpp>

TEST (ed25519, signing)
{
    mu_coin::uint256_union prv;
    mu_coin::uint256_union pub;
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
    mu_coin::uint256_union message;
    mu_coin::uint512_union signature;
    ed25519_sign (message.bytes.data (), sizeof (message.bytes), prv.bytes.data (), pub.bytes.data (), signature.bytes.data ());
    auto valid1 (ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), pub.bytes.data (), signature.bytes.data ()));
    ASSERT_EQ (0, valid1);
    signature.bytes [32] ^= 0x1;
    auto valid2 (ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), pub.bytes.data (), signature.bytes.data ()));
    ASSERT_NE (0, valid2);
}

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
    ASSERT_EQ (mu_coin::uint256_t (1), bytes1.number ());
    mu_coin::uint512_union bytes2;
    bytes2.clear ();
    bytes2.bytes [63] = 1;
    ASSERT_EQ (mu_coin::uint512_t (1), bytes2.number ());
}

TEST (transaction_block, empty)
{
    mu_coin::keypair key1;
    mu_coin::send_block block;
    mu_coin::send_input entry (key1.pub, 0, 13);
    block.inputs.push_back (entry);
    ASSERT_EQ (1, block.inputs.size ());
    mu_coin::uint256_union hash (block.hash ());
    block.signatures.push_back (mu_coin::uint512_union ());
    mu_coin::sign_message (key1.prv, key1.pub, hash, block.signatures.back ());
    ASSERT_FALSE (mu_coin::validate_message (key1.pub, hash, block.signatures.back ()));
    block.signatures [0].bytes [32] ^= 0x1;
    ASSERT_TRUE (mu_coin::validate_message (key1.pub, hash, block.signatures.back ()));
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
    mu_coin::send_input entry1 (key1.pub, 0, 43);
    block1.inputs.push_back (entry1);
    mu_coin::send_input entry2 (key2.pub, 0, 17);
    block1.inputs.push_back (entry2);
    block1.signatures.push_back (mu_coin::uint512_union ());
    block1.signatures.push_back (mu_coin::uint512_union ());
    auto hash (block1.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash, block1.signatures [0]);
    mu_coin::sign_message (key2.prv, key2.pub, hash, block1.signatures [1]);
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
    block1.serialize (stream1);
    mu_coin::byte_read_stream stream2 (stream1.data, stream1.size);
    mu_coin::receive_block block2;
    auto error (block2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (block1, block2);
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

TEST (send_block, deserialize)
{
    mu_coin::send_block block1;
    mu_coin::byte_write_stream stream1;
    mu_coin::serialize_block (stream1, block1);
    mu_coin::byte_read_stream stream2 (stream1.data, stream1.size);
    auto block2 (mu_coin::deserialize_block (stream2));
    ASSERT_NE (nullptr, block2);
    ASSERT_EQ (block1, *block2);
}

TEST (send_block, copy)
{
    mu_coin::send_block block1;
    block1.inputs.push_back (mu_coin::send_input ());
    block1.outputs.push_back (mu_coin::send_output ());
    block1.signatures.push_back (mu_coin::uint512_union ());
    mu_coin::send_block block2 (block1);
    ASSERT_EQ (block1, block2);
}