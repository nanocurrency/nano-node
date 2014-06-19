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

TEST (publish_con, serialization)
{
    mu_coin::block_hash hash;
    mu_coin::publish_con con1 {hash};
    mu_coin::keypair key1;
    mu_coin::signature signature;
    mu_coin::sign_message (key1.prv, key1.pub, hash, signature);
    mu_coin::authorization authorization {key1.pub, signature};
    con1.authorizations.push_back (authorization);
    mu_coin::byte_write_stream stream1;
    con1.serialize (stream1);
    mu_coin::byte_read_stream stream2 (stream1.data, stream1.size);
    mu_coin::publish_con con2;
    con2.deserialize (stream2);
    ASSERT_EQ (con1, con2);
}

TEST (block_store, empty_blocks)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    auto begin (store.blocks_begin ());
    auto end (store.blocks_end ());
    ASSERT_EQ (end, begin);
}

TEST (block_store, empty_accounts)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    auto begin (store.latest_begin ());
    auto end (store.latest_end ());
    ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::send_block block1;
    store.block_put (block1.hash (), block1);
    auto begin (store.blocks_begin ());
    auto end (store.blocks_end ());
    ASSERT_NE (end, begin);
    auto hash1 (begin->first);
    ASSERT_EQ (block1.hash (), hash1);
    auto block2 (begin->second->clone ());
    ASSERT_EQ (block1, *block2);
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, one_account)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::address address;
    mu_coin::block_hash hash;
    store.latest_put (address, hash);
    auto begin (store.latest_begin ());
    auto end (store.latest_end ());
    ASSERT_NE (end, begin);
    ASSERT_EQ (address, begin->first);
    ASSERT_EQ (hash, begin->second);
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::send_block block1;
    block1.outputs.push_back (mu_coin::send_output (1, 2));
    store.block_put (block1.hash (), block1);
    mu_coin::send_block block2;
    block2.outputs.push_back (mu_coin::send_output (3, 4));
    store.block_put (block2.hash (), block2);
    auto begin (store.blocks_begin ());
    auto end (store.blocks_end ());
    ASSERT_NE (end, begin);
    auto hash1 (begin->first);
    ASSERT_EQ (block1.hash (), hash1);
    auto block3 (begin->second->clone ());
    ASSERT_EQ (block1, *block3);
    ++begin;
    ASSERT_NE (end, begin);
    auto hash2 (begin->first);
    ASSERT_EQ (block2.hash (), hash2);
    auto block4 (begin->second->clone ());
    ASSERT_EQ (block2, *block4);
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, two_account)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::address address1 (1);
    mu_coin::block_hash hash1 (2);
    mu_coin::address address2 (3);
    mu_coin::block_hash hash2 (4);
    store.latest_put (address1, hash1);
    store.latest_put (address2, hash2);
    auto begin (store.latest_begin ());
    auto end (store.latest_end ());
    ASSERT_NE (end, begin);
    ASSERT_EQ (address1, begin->first);
    ASSERT_EQ (hash1, begin->second);
    ++begin;
    ASSERT_NE (end, begin);
    ASSERT_EQ (address2, begin->first);
    ASSERT_EQ (hash2, begin->second);
    ++begin;
    ASSERT_EQ (end, begin);
}