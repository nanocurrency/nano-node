#include <boost/property_tree/json_parser.hpp>

#include <fstream>

#include <gtest/gtest.h>

#include <banano/lib/interface.h>
#include <banano/node/common.hpp>
#include <banano/node/node.hpp>

#include <ed25519-donna/ed25519.h>

TEST (ed25519, signing)
{
	rai::uint256_union prv (0);
	rai::uint256_union pub;
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
	rai::uint256_union message (0);
	rai::uint512_union signature;
	ed25519_sign (message.bytes.data (), sizeof (message.bytes), prv.bytes.data (), pub.bytes.data (), signature.bytes.data ());
	auto valid1 (ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), pub.bytes.data (), signature.bytes.data ()));
	ASSERT_EQ (0, valid1);
	signature.bytes[32] ^= 0x1;
	auto valid2 (ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), pub.bytes.data (), signature.bytes.data ()));
	ASSERT_NE (0, valid2);
}

TEST (transaction_block, empty)
{
	rai::keypair key1;
	rai::send_block block (0, 1, 13, key1.prv, key1.pub, 2);
	rai::uint256_union hash (block.hash ());
	ASSERT_FALSE (rai::validate_message (key1.pub, hash, block.signature));
	block.signature.bytes[32] ^= 0x1;
	ASSERT_TRUE (rai::validate_message (key1.pub, hash, block.signature));
}

TEST (block, send_serialize)
{
	rai::send_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	std::vector<uint8_t> bytes;
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
	rai::send_block block2 (error, stream2);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, send_serialize_json)
{
	rai::send_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	std::string string1;
	block1.serialize_json (string1);
	ASSERT_NE (0, string1.size ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (string1);
	boost::property_tree::read_json (istream, tree1);
	bool error;
	rai::send_block block2 (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, receive_serialize)
{
	rai::receive_block block1 (0, 1, rai::keypair ().prv, 3, 4);
	rai::keypair key1;
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream1 (bytes);
		block1.serialize (stream1);
	}
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error;
	rai::receive_block block2 (error, stream2);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, receive_serialize_json)
{
	rai::receive_block block1 (0, 1, rai::keypair ().prv, 3, 4);
	std::string string1;
	block1.serialize_json (string1);
	ASSERT_NE (0, string1.size ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (string1);
	boost::property_tree::read_json (istream, tree1);
	bool error;
	rai::receive_block block2 (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, open_serialize_json)
{
	rai::open_block block1 (0, 1, 0, rai::keypair ().prv, 0, 0);
	std::string string1;
	block1.serialize_json (string1);
	ASSERT_NE (0, string1.size ());
	boost::property_tree::ptree tree1;
	std::stringstream istream (string1);
	boost::property_tree::read_json (istream, tree1);
	bool error;
	rai::open_block block2 (error, tree1);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (block, change_serialize_json)
{
	rai::change_block block1 (0, 1, rai::keypair ().prv, 3, 4);
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
	text[5] = '!';
	rai::uint512_union output;
	auto error (output.decode_hex (text));
	ASSERT_TRUE (error);
}

TEST (uint512_union, max)
{
	rai::uint512_union input (std::numeric_limits<rai::uint512_t>::max ());
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
	rai::uint512_union input (std::numeric_limits<rai::uint512_t>::max ());
	std::string text;
	input.encode_hex (text);
	text.push_back (0);
	rai::uint512_union output;
	auto error (output.decode_hex (text));
	ASSERT_TRUE (error);
}

TEST (send_block, deserialize)
{
	rai::send_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	ASSERT_EQ (block1.hash (), block1.hash ());
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream1 (bytes);
		block1.serialize (stream1);
	}
	ASSERT_EQ (rai::send_block::size, bytes.size ());
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error;
	rai::send_block block2 (error, stream2);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (receive_block, deserialize)
{
	rai::receive_block block1 (0, 1, rai::keypair ().prv, 3, 4);
	ASSERT_EQ (block1.hash (), block1.hash ());
	block1.hashables.previous = 2;
	block1.hashables.source = 4;
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream1 (bytes);
		block1.serialize (stream1);
	}
	ASSERT_EQ (rai::receive_block::size, bytes.size ());
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error;
	rai::receive_block block2 (error, stream2);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (open_block, deserialize)
{
	rai::open_block block1 (0, 1, 0, rai::keypair ().prv, 0, 0);
	ASSERT_EQ (block1.hash (), block1.hash ());
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		block1.serialize (stream);
	}
	ASSERT_EQ (rai::open_block::size, bytes.size ());
	rai::bufferstream stream (bytes.data (), bytes.size ());
	bool error;
	rai::open_block block2 (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (block1, block2);
}

TEST (change_block, deserialize)
{
	rai::change_block block1 (1, 2, rai::keypair ().prv, 4, 5);
	ASSERT_EQ (block1.hash (), block1.hash ());
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream1 (bytes);
		block1.serialize (stream1);
	}
	ASSERT_EQ (rai::change_block::size, bytes.size ());
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

TEST (frontier_req, serialization)
{
	rai::frontier_req request1;
	request1.start = 1;
	request1.age = 2;
	request1.count = 3;
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	auto error (false);
	rai::bufferstream stream (bytes.data (), bytes.size ());
	rai::message_header header (error, stream);
	ASSERT_FALSE (error);
	rai::frontier_req request2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (request1, request2);
}

TEST (block, publish_req_serialization)
{
	rai::keypair key1;
	rai::keypair key2;
	auto block (std::unique_ptr<rai::send_block> (new rai::send_block (0, key2.pub, 200, rai::keypair ().prv, 2, 3)));
	rai::publish req (std::move (block));
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	rai::message_header header (error, stream2);
	ASSERT_FALSE (error);
	rai::publish req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (*req.block, *req2.block);
}

TEST (block, confirm_req_serialization)
{
	rai::keypair key1;
	rai::keypair key2;
	auto block (std::unique_ptr<rai::send_block> (new rai::send_block (0, key2.pub, 200, rai::keypair ().prv, 2, 3)));
	rai::confirm_req req (std::move (block));
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	rai::message_header header (error, stream2);
	rai::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (*req.block, *req2.block);
}

TEST (state_block, serialization)
{
	rai::keypair key1;
	rai::keypair key2;
	rai::state_block block1 (key1.pub, 1, key2.pub, 2, 4, key1.prv, key1.pub, 5);
	ASSERT_EQ (key1.pub, block1.hashables.account);
	ASSERT_EQ (rai::block_hash (1), block1.previous ());
	ASSERT_EQ (key2.pub, block1.hashables.representative);
	ASSERT_EQ (rai::amount (2), block1.hashables.balance);
	ASSERT_EQ (rai::uint256_union (4), block1.hashables.link);
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		block1.serialize (stream);
	}
	ASSERT_EQ (0x5, bytes[215]); // Ensure work is serialized big-endian
	ASSERT_EQ (rai::state_block::size, bytes.size ());
	bool error1;
	rai::bufferstream stream (bytes.data (), bytes.size ());
	rai::state_block block2 (error1, stream);
	ASSERT_FALSE (error1);
	ASSERT_EQ (block1, block2);
	block2.hashables.account.clear ();
	block2.hashables.previous.clear ();
	block2.hashables.representative.clear ();
	block2.hashables.balance.clear ();
	block2.hashables.link.clear ();
	block2.signature.clear ();
	block2.work = 0;
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	ASSERT_FALSE (block2.deserialize (stream2));
	ASSERT_EQ (block1, block2);
	std::string json;
	block1.serialize_json (json);
	std::stringstream body (json);
	boost::property_tree::ptree tree;
	boost::property_tree::read_json (body, tree);
	bool error2;
	rai::state_block block3 (error2, tree);
	ASSERT_FALSE (error2);
	ASSERT_EQ (block1, block3);
	block3.hashables.account.clear ();
	block3.hashables.previous.clear ();
	block3.hashables.representative.clear ();
	block3.hashables.balance.clear ();
	block3.hashables.link.clear ();
	block3.signature.clear ();
	block3.work = 0;
	ASSERT_FALSE (block3.deserialize_json (tree));
	ASSERT_EQ (block1, block3);
}

TEST (state_block, hashing)
{
	rai::keypair key;
	rai::state_block block (key.pub, 0, key.pub, 0, 0, key.prv, key.pub, 0);
	auto hash (block.hash ());
	block.hashables.account.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.account.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
	block.hashables.previous.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.previous.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
	block.hashables.representative.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.representative.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
	block.hashables.balance.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.balance.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
	block.hashables.link.bytes[0] ^= 0x1;
	ASSERT_NE (hash, block.hash ());
	block.hashables.link.bytes[0] ^= 0x1;
	ASSERT_EQ (hash, block.hash ());
}
