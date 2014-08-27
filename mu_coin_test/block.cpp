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
    block.hashables.previous.clear ();
    block.hashables.balance = 13;
    mu_coin::uint256_union hash (block.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash, block.signature);
    ASSERT_FALSE (mu_coin::validate_message (key1.pub, hash, block.signature));
    block.signature.bytes [32] ^= 0x1;
    ASSERT_TRUE (mu_coin::validate_message (key1.pub, hash, block.signature));
}

TEST (send_block, empty_send_serialize)
{
    mu_coin::send_block block1;
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream1 (bytes);
        block1.serialize (stream1);
    }
    auto data (bytes.data ());
    auto size (bytes.size ());
    ASSERT_NE (nullptr, data);
    ASSERT_NE (0, size);
    mu_coin::bufferstream stream2 (data, size);
    mu_coin::send_block block2;
    block2.deserialize (stream2);
    ASSERT_EQ (block1, block2);
}

TEST (send_block, receive_serialize)
{
    mu_coin::receive_block block1;
    mu_coin::keypair key1;
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream1 (bytes);
        block1.serialize (stream1);
    }
    mu_coin::bufferstream stream2 (bytes.data (), bytes.size ());
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

TEST (uint256_union, max_hex)
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

TEST (uint256_union, max_dec)
{
    mu_coin::uint256_union input (std::numeric_limits <mu_coin::uint256_t>::max ());
    std::string text;
    input.encode_dec (text);
    mu_coin::uint256_union output;
    auto error (output.decode_dec (text));
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
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream1 (bytes);
        mu_coin::serialize_block (stream1, block1);
    }
    mu_coin::bufferstream stream2 (bytes.data (), bytes.size ());
    auto block2 (mu_coin::deserialize_block (stream2));
    ASSERT_NE (nullptr, block2);
    ASSERT_EQ (block1, *block2);
}

TEST (receive_block, deserialize)
{
    mu_coin::receive_block block1;
    block1.hashables.previous = 2;
    block1.hashables.source = 4;
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream1 (bytes);
        mu_coin::serialize_block (stream1, block1);
    }
    mu_coin::bufferstream stream2 (bytes.data (), bytes.size ());
    auto block2 (mu_coin::deserialize_block (stream2));
    ASSERT_NE (nullptr, block2);
    ASSERT_EQ (block1, *block2);
}

TEST (send_block, copy)
{
    mu_coin::send_block block1;
    mu_coin::send_block block2 (block1);
    ASSERT_EQ (block1, block2);
}

TEST (confirm_ack, serialization)
{
    mu_coin::uint256_union session;
    mu_coin::confirm_ack con1;
	con1.session = session;
    mu_coin::keypair key1;
    con1.address = key1.pub;
    mu_coin::sign_message (key1.prv, key1.pub, con1.hash (), con1.signature);
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream1 (bytes);
        con1.serialize (stream1);
    }
    mu_coin::bufferstream stream2 (bytes.data (), bytes.size ());
    mu_coin::confirm_ack con2;
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

TEST (block_store, frontier_retrieval)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::address address1;
    mu_coin::frontier frontier1;
    store.latest_put (address1, frontier1);
    mu_coin::frontier frontier2;
    store.latest_get (address1, frontier2);
    ASSERT_EQ (frontier1, frontier2);
}

TEST (block_store, one_account)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::address address;
    mu_coin::block_hash hash;
    store.latest_put (address, {hash, address, 100});
    auto begin (store.latest_begin ());
    auto end (store.latest_end ());
    ASSERT_NE (end, begin);
    ASSERT_EQ (address, begin->first);
    ASSERT_EQ (hash, begin->second.hash);
    ASSERT_EQ (100, begin->second.time);
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::send_block block1;
    block1.hashables.destination = 1;
    block1.hashables.balance = 2;
    std::vector <mu_coin::block_hash> hashes;
    std::vector <mu_coin::send_block> blocks;
    hashes.push_back (block1.hash ());
    blocks.push_back (block1);
    store.block_put (hashes [0], block1);
    mu_coin::send_block block2;
    block2.hashables.destination = 3;
    block2.hashables.balance = 4;
    hashes.push_back (block2.hash ());
    blocks.push_back (block2);
    store.block_put (hashes [1], block2);
    auto begin (store.blocks_begin ());
    auto end (store.blocks_end ());
    ASSERT_NE (end, begin);
    auto hash1 (begin->first);
    ASSERT_NE (hashes.end (), std::find (hashes.begin (), hashes.end (), hash1));
    auto block3 (begin->second->clone ());
    ASSERT_NE (blocks.end (), std::find (blocks.begin (), blocks.end (), *block3));
    ++begin;
    ASSERT_NE (end, begin);
    auto hash2 (begin->first);
    ASSERT_NE (hashes.end (), std::find (hashes.begin (), hashes.end (), hash2));
    auto block4 (begin->second->clone ());
    ASSERT_NE (blocks.end (), std::find (blocks.begin (), blocks.end (), *block4));
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
    store.latest_put (address1, {hash1, address1, 100});
    store.latest_put (address2, {hash2, address2, 200});
    auto begin (store.latest_begin ());
    auto end (store.latest_end ());
    ASSERT_NE (end, begin);
    ASSERT_EQ (address1, begin->first);
    ASSERT_EQ (hash1, begin->second.hash);
    ASSERT_EQ (100, begin->second.time);
    ++begin;
    ASSERT_NE (end, begin);
    ASSERT_EQ (address2, begin->first);
    ASSERT_EQ (hash2, begin->second.hash);
    ASSERT_EQ (200, begin->second.time);
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::address address1 (1);
    mu_coin::block_hash hash1 (2);
    mu_coin::address address2 (3);
    mu_coin::block_hash hash2 (4);
    store.latest_put (address1, {hash1, address1, 100});
    store.latest_put (address2, {hash2, address2, 200});
    auto first (store.latest_begin ());
    auto second (store.latest_begin ());
    ++second;
    auto find1 (store.latest_begin (1));
    ASSERT_EQ (first, find1);
    auto find2 (store.latest_begin (3));
    ASSERT_EQ (second, find2);
    auto find3 (store.latest_begin (2));
    ASSERT_EQ (second, find3);
}

TEST (gap_cache, add_new)
{
    mu_coin::gap_cache cache;
    mu_coin::send_block block1;
    cache.add (mu_coin::send_block (block1), block1.previous ());
    ASSERT_NE (cache.blocks.end (), cache.blocks.find (block1.previous ()));
}

TEST (gap_cache, add_existing)
{
    mu_coin::gap_cache cache;
    mu_coin::send_block block1;
    auto previous (block1.previous ());
    cache.add (block1, previous);
    auto existing1 (cache.blocks.find (previous));
    ASSERT_NE (cache.blocks.end (), existing1);
    auto arrival (existing1->arrival);
    while (arrival == std::chrono::system_clock::now ());
    cache.add (block1, previous);
    ASSERT_EQ (1, cache.blocks.size ());
    auto existing2 (cache.blocks.find (previous));
    ASSERT_NE (cache.blocks.end (), existing2);
    ASSERT_GT (existing2->arrival, arrival);
}

TEST (gap_cache, comparison)
{
    mu_coin::gap_cache cache;
    mu_coin::send_block block1;
    block1.hashables.previous.clear ();
    auto previous1 (block1.previous ());
    cache.add (mu_coin::send_block (block1), previous1);
    auto existing1 (cache.blocks.find (previous1));
    ASSERT_NE (cache.blocks.end (), existing1);
    auto arrival (existing1->arrival);
    mu_coin::send_block block3;
    block3.hashables.previous = 42;
    auto previous2 (block3.previous ());
    cache.add (mu_coin::send_block (block3), previous2);
    ASSERT_EQ (2, cache.blocks.size ());
    auto existing2 (cache.blocks.find (previous2));
    ASSERT_NE (cache.blocks.end (), existing2);
    ASSERT_GT (existing2->arrival, arrival);
    ASSERT_EQ (arrival, cache.blocks.get <1> ().begin ()->arrival);
}

TEST (gap_cache, limit)
{
    mu_coin::gap_cache cache;
    for (auto i (0); i < cache.max * 2; ++i)
    {
        mu_coin::send_block block1;
        block1.hashables.previous = i;
        auto previous (block1.previous ());
        cache.add (mu_coin::send_block (block1), previous);
    }
    ASSERT_EQ (cache.max, cache.blocks.size ());
}

TEST (frontier_req, serialization)
{
    mu_coin::frontier_req request1;
    request1.start = 1;
    request1.age = 2;
    request1.count = 3;
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream (bytes);
        request1.serialize (stream);
    }
    mu_coin::bufferstream buffer (bytes.data (), bytes.size ());
    mu_coin::frontier_req request2;
    ASSERT_EQ (false, request2.deserialize (buffer));
    ASSERT_EQ (request1, request2);
}

TEST (keepalive_ack, serialization)
{
	mu_coin::keepalive_ack request1;
	std::vector <uint8_t> bytes;
	{
		mu_coin::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	mu_coin::keepalive_ack request2;
	mu_coin::bufferstream buffer (bytes.data (), bytes.size ());
	ASSERT_FALSE (request2.deserialize (buffer));
	ASSERT_EQ (request1, request2);
}