#include <gtest/gtest.h>
#include <rai/core/core.hpp>
#include <fstream>

#include <boost/property_tree/json_parser.hpp>

TEST (ed25519, signing)
{
    rai::uint256_union prv;
    rai::uint256_union pub;
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
    rai::uint256_union message;
    rai::uint512_union signature;
    ed25519_sign (message.bytes.data (), sizeof (message.bytes), prv.bytes.data (), pub.bytes.data (), signature.bytes.data ());
    auto valid1 (ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), pub.bytes.data (), signature.bytes.data ()));
    ASSERT_EQ (0, valid1);
    signature.bytes [32] ^= 0x1;
    auto valid2 (ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), pub.bytes.data (), signature.bytes.data ()));
    ASSERT_NE (0, valid2);
}

TEST (transaction_block, empty)
{
    rai::keypair key1;
    rai::send_block block;
    block.hashables.previous.clear ();
    block.hashables.balance = 13;
    rai::uint256_union hash (block.hash ());
    rai::sign_message (key1.prv, key1.pub, hash, block.signature);
    ASSERT_FALSE (rai::validate_message (key1.pub, hash, block.signature));
    block.signature.bytes [32] ^= 0x1;
    ASSERT_TRUE (rai::validate_message (key1.pub, hash, block.signature));
}

TEST (block, send_serialize)
{
    rai::send_block block1;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream1 (bytes);
        block1.serialize (stream1);
    }
    auto data (bytes.data ());
    auto size (bytes.size ());
    ASSERT_NE (nullptr, data);
    ASSERT_NE (0, size);
    rai::bufferstream stream2 (data, size);
    rai::send_block block2;
    ASSERT_FALSE (block2.deserialize (stream2));
    ASSERT_EQ (block1, block2);
}

TEST (block, change_serialize)
{
    rai::change_block block1 (1, 2, 3, 4, 5);
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream1 (bytes);
        block1.serialize (stream1);
    }
    auto data (bytes.data ());
    auto size (bytes.size ());
    ASSERT_NE (nullptr, data);
    ASSERT_NE (0, size);
    rai::bufferstream stream2 (data, size);
    bool error;
    rai::change_block block2 (error, stream2);
    ASSERT_FALSE (error);
    ASSERT_EQ (block1, block2);
}

TEST (block, send_serialize_json)
{
    rai::send_block block1;
    std::string string1;
    block1.serialize_json (string1);
    ASSERT_NE (0, string1.size ());
    rai::send_block block2;
    boost::property_tree::ptree tree1;
    std::stringstream istream (string1);
    boost::property_tree::read_json (istream, tree1);
    ASSERT_FALSE (block2.deserialize_json (tree1));
    ASSERT_EQ (block1, block2);
}

TEST (block, receive_serialize)
{
    rai::receive_block block1;
    rai::keypair key1;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream1 (bytes);
        block1.serialize (stream1);
    }
    rai::bufferstream stream2 (bytes.data (), bytes.size ());
    rai::receive_block block2;
    auto error (block2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (block1, block2);
}

TEST (block, receive_serialize_json)
{
    rai::receive_block block1;
    std::string string1;
    block1.serialize_json (string1);
    ASSERT_NE (0, string1.size ());
    rai::receive_block block2;
    boost::property_tree::ptree tree1;
    std::stringstream istream (string1);
    boost::property_tree::read_json (istream, tree1);
    ASSERT_FALSE (block2.deserialize_json (tree1));
    ASSERT_EQ (block1, block2);
}

TEST (block, open_serialize_json)
{
    rai::open_block block1;
    std::string string1;
    block1.serialize_json (string1);
    ASSERT_NE (0, string1.size ());
    rai::open_block block2;
    boost::property_tree::ptree tree1;
    std::stringstream istream (string1);
    boost::property_tree::read_json (istream, tree1);
    ASSERT_FALSE (block2.deserialize_json (tree1));
    ASSERT_EQ (block1, block2);
}

TEST (block, change_serialize_json)
{
    rai::change_block block1 (rai::account (1), rai::block_hash (2), 0, rai::private_key (3), rai::public_key (4));
    std::string string1;
    block1.serialize_json (string1);
    ASSERT_NE (0, string1.size ());
    boost::property_tree::ptree tree1;
    std::stringstream istream (string1);
    boost::property_tree::read_json (istream, tree1);
    bool error;
    rai::change_block block2 (error, tree1);
    ASSERT_FALSE (error);
    ASSERT_EQ (block1, block2);
}

TEST (uint512_union, parse_zero)
{
    rai::uint512_union input (rai::uint512_t (0));
    std::string text;
    input.encode_hex (text);
    rai::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint512_union, parse_zero_short)
{
    std::string text ("0");
    rai::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_TRUE (output.number ().is_zero ());
}

TEST (uint512_union, parse_one)
{
    rai::uint512_union input (rai::uint512_t (1));
    std::string text;
    input.encode_hex (text);
    rai::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_EQ (1, output.number ());
}

TEST (uint512_union, parse_error_symbol)
{
    rai::uint512_union input (rai::uint512_t (1000));
    std::string text;
    input.encode_hex (text);
    text [5] = '!';
    rai::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_TRUE (error);
}

TEST (uint512_union, max)
{
    rai::uint512_union input (std::numeric_limits <rai::uint512_t>::max ());
    std::string text;
    input.encode_hex (text);
    rai::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_FALSE (error);
    ASSERT_EQ (input, output);
    ASSERT_EQ (rai::uint512_t ("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), output.number ());
}

TEST (uint512_union, parse_error_overflow)
{
    rai::uint512_union input (std::numeric_limits <rai::uint512_t>::max ());
    std::string text;
    input.encode_hex (text);
    text.push_back (0);
    rai::uint512_union output;
    auto error (output.decode_hex (text));
    ASSERT_TRUE (error);
}

TEST (send_block, deserialize)
{
    rai::send_block block1;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream1 (bytes);
        rai::serialize_block (stream1, block1);
    }
    rai::bufferstream stream2 (bytes.data (), bytes.size ());
    auto block2 (rai::deserialize_block (stream2));
    ASSERT_NE (nullptr, block2);
    ASSERT_EQ (block1, *block2);
}

TEST (receive_block, deserialize)
{
    rai::receive_block block1;
    block1.hashables.previous = 2;
    block1.hashables.source = 4;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream1 (bytes);
        rai::serialize_block (stream1, block1);
    }
    rai::bufferstream stream2 (bytes.data (), bytes.size ());
    auto block2 (rai::deserialize_block (stream2));
    ASSERT_NE (nullptr, block2);
    ASSERT_EQ (block1, *block2);
}

TEST (send_block, copy)
{
    rai::send_block block1;
    rai::send_block block2 (block1);
    ASSERT_EQ (block1, block2);
}

TEST (gap_cache, add_new)
{
    rai::gap_cache cache;
    rai::send_block block1;
    cache.add (rai::send_block (block1), block1.previous ());
    ASSERT_NE (cache.blocks.end (), cache.blocks.find (block1.previous ()));
}

TEST (gap_cache, add_existing)
{
    rai::gap_cache cache;
    rai::send_block block1;
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
    rai::gap_cache cache;
    rai::send_block block1;
    block1.hashables.previous.clear ();
    auto previous1 (block1.previous ());
    cache.add (rai::send_block (block1), previous1);
    auto existing1 (cache.blocks.find (previous1));
    ASSERT_NE (cache.blocks.end (), existing1);
    auto arrival (existing1->arrival);
    while (std::chrono::system_clock::now () == arrival);
    rai::send_block block3;
    block3.hashables.previous = 42;
    auto previous2 (block3.previous ());
    cache.add (rai::send_block (block3), previous2);
    ASSERT_EQ (2, cache.blocks.size ());
    auto existing2 (cache.blocks.find (previous2));
    ASSERT_NE (cache.blocks.end (), existing2);
    ASSERT_GT (existing2->arrival, arrival);
    ASSERT_EQ (arrival, cache.blocks.get <1> ().begin ()->arrival);
}

TEST (gap_cache, limit)
{
    rai::gap_cache cache;
    for (auto i (0); i < cache.max * 2; ++i)
    {
        rai::send_block block1;
        block1.hashables.previous = i;
        auto previous (block1.previous ());
        cache.add (rai::send_block (block1), previous);
    }
    ASSERT_EQ (cache.max, cache.blocks.size ());
}

TEST (frontier_req, serialization)
{
    rai::frontier_req request1;
    request1.start = 1;
    request1.age = 2;
    request1.count = 3;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        request1.serialize (stream);
    }
    rai::bufferstream buffer (bytes.data (), bytes.size ());
    rai::frontier_req request2;
    ASSERT_FALSE (request2.deserialize (buffer));
    ASSERT_EQ (request1, request2);
}

TEST (work, one)
{
    rai::work work (rai::block::publish_work);
    rai::uint256_union seed (0x0123456789abcdef);
    uint64_t nonce (0x0123456789abcdef);
    CryptoPP::SHA3 hash (32);
    auto value1 (work.generate (hash, seed, nonce));
	auto value2 (work.generate (hash, seed, nonce));
	ASSERT_EQ (value1, value2);
}

TEST (work, create)
{
    rai::uint256_union source (1);
    rai::work work (rai::block::publish_work);
    auto begin1 (std::chrono::high_resolution_clock::now ());
    auto value (work.create (source));
    auto end1 (std::chrono::high_resolution_clock::now ());
    EXPECT_FALSE (work.validate (source, value));
    auto end2 (std::chrono::high_resolution_clock::now ());
    std::cerr << boost::str (boost::format ("Generation time: %1%us validation time: %2%us\n") % std::chrono::duration_cast <std::chrono::microseconds> (end1 - begin1).count () % std::chrono::duration_cast <std::chrono::microseconds> (end2 - end1).count ());
}

TEST (shared_work, validate)
{
	rai::system system (24000, 1);
	rai::shared_work work (*system.clients [0]);
	rai::send_block send_block;
	ASSERT_TRUE (work.validate (send_block));
	ASSERT_EQ (1, work.insufficient_work_count);
	send_block.work = system.clients [0]->create_work (send_block);
	ASSERT_FALSE (work.validate (send_block));
}

TEST (block, publish_req_serialization)
{
    auto block (std::unique_ptr <rai::send_block> (new rai::send_block));
    rai::keypair key1;
    rai::keypair key2;
    block->hashables.previous.clear ();
    block->hashables.balance = 200;
    block->hashables.destination = key2.pub;
    rai::publish req (std::move (block));
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        req.serialize (stream);
    }
    rai::publish req2;
    rai::bufferstream stream2 (bytes.data (), bytes.size ());
    auto error (req2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (req, req2);
    ASSERT_EQ (*req.block, *req2.block);
}

TEST (block, confirm_req_serialization)
{
    auto block (std::unique_ptr <rai::send_block> (new rai::send_block));
    rai::keypair key1;
    rai::keypair key2;
    block->hashables.previous.clear ();
    block->hashables.balance = 200;
    block->hashables.destination = key2.pub;
    rai::confirm_req req (std::move (block));
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        req.serialize (stream);
    }
    rai::confirm_req req2;
    rai::bufferstream stream2 (bytes.data (), bytes.size ());
    auto error (req2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (req, req2);
    ASSERT_EQ (*req.block, *req2.block);
}