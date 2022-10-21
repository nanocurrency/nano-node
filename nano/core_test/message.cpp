#include <nano/node/common.hpp>
#include <nano/node/network.hpp>
#include <nano/secure/buffer.hpp>

#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>
#include <boost/variant/get.hpp>

TEST (message, keepalive_serialization)
{
	nano::keepalive request1{ nano::dev::network_params.network };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	auto error (false);
	nano::bufferstream stream (bytes.data (), bytes.size ());
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	nano::keepalive request2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (request1, request2);
}

TEST (message, keepalive_deserialize)
{
	nano::keepalive message1{ nano::dev::network_params.network };
	message1.peers[0] = nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message1.serialize (stream);
	}
	nano::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::keepalive, header.type);
	nano::keepalive message2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (message1.peers, message2.peers);
}

TEST (message, publish_serialization)
{
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (0)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (5)
				 .build_shared ();
	nano::publish publish{ nano::dev::network_params.network, block };
	ASSERT_EQ (nano::block_type::send, publish.header.block_type ());
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		publish.header.serialize (stream);
	}
	ASSERT_EQ (8, bytes.size ());
	ASSERT_EQ (0x52, bytes[0]);
	ASSERT_EQ (0x41, bytes[1]);
	ASSERT_EQ (nano::dev::network_params.network.protocol_version, bytes[2]);
	ASSERT_EQ (nano::dev::network_params.network.protocol_version, bytes[3]);
	ASSERT_EQ (nano::dev::network_params.network.protocol_version_min, bytes[4]);
	ASSERT_EQ (static_cast<uint8_t> (nano::message_type::publish), bytes[5]);
	ASSERT_EQ (0x00, bytes[6]); // extensions
	ASSERT_EQ (static_cast<uint8_t> (nano::block_type::send), bytes[7]);
	nano::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::dev::network_params.network.protocol_version_min, header.version_min);
	ASSERT_EQ (nano::dev::network_params.network.protocol_version, header.version_using);
	ASSERT_EQ (nano::dev::network_params.network.protocol_version, header.version_max);
	ASSERT_EQ (nano::message_type::publish, header.type);
}

TEST (message, confirm_ack_hash_serialization)
{
	std::vector<nano::block_hash> hashes;
	for (auto i (hashes.size ()); i < nano::network::confirm_ack_hashes_max; i++)
	{
		nano::keypair key1;
		nano::block_hash previous;
		nano::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		nano::block_builder builder;
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (key1.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		hashes.push_back (block->hash ());
	}
	nano::keypair representative1;
	auto vote (std::make_shared<nano::vote> (representative1.pub, representative1.prv, 0, 0, hashes));
	nano::confirm_ack con1{ nano::dev::network_params.network, vote };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	nano::message_header header (error, stream2);
	nano::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	ASSERT_EQ (hashes, con2.vote->hashes);
	// Check overflow with max hashes
	ASSERT_EQ (header.count_get (), hashes.size ());
	ASSERT_EQ (header.block_type (), nano::block_type::not_a_block);
}

TEST (message, confirm_req_serialization)
{
	nano::keypair key1;
	nano::keypair key2;
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (0)
				 .destination (key2.pub)
				 .balance (200)
				 .sign (nano::keypair ().prv, 2)
				 .work (3)
				 .build_shared ();
	nano::confirm_req req{ nano::dev::network_params.network, block };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header (error, stream2);
	nano::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (*req.block, *req2.block);
}

TEST (message, confirm_req_hash_serialization)
{
	nano::keypair key1;
	nano::keypair key2;
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (key2.pub)
				 .balance (200)
				 .sign (nano::keypair ().prv, 2)
				 .work (3)
				 .build ();
	nano::confirm_req req{ nano::dev::network_params.network, block->hash (), block->root () };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header (error, stream2);
	nano::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (header.block_type (), nano::block_type::not_a_block);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
}

TEST (message, confirm_req_hash_batch_serialization)
{
	nano::keypair key;
	nano::keypair representative;
	std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes;
	nano::block_builder builder;
	auto open = builder
				.state ()
				.account (key.pub)
				.previous (0)
				.representative (representative.pub)
				.balance (2)
				.link (4)
				.sign (key.prv, key.pub)
				.work (5)
				.build ();
	roots_hashes.push_back (std::make_pair (open->hash (), open->root ()));
	for (auto i (roots_hashes.size ()); i < 7; i++)
	{
		nano::keypair key1;
		nano::block_hash previous;
		nano::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (representative.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		roots_hashes.push_back (std::make_pair (block->hash (), block->root ()));
	}
	roots_hashes.push_back (std::make_pair (open->hash (), open->root ()));
	nano::confirm_req req{ nano::dev::network_params.network, roots_hashes };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header (error, stream2);
	nano::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (req.roots_hashes, roots_hashes);
	ASSERT_EQ (req2.roots_hashes, roots_hashes);
	ASSERT_EQ (header.block_type (), nano::block_type::not_a_block);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
}

// this unit test checks that conversion of message_header to string works as expected
TEST (message, message_header_to_string)
{
	// calculate expected string
	int maxver = nano::dev::network_params.network.protocol_version;
	int minver = nano::dev::network_params.network.protocol_version_min;
	std::stringstream ss;
	ss << "NetID: 5241(dev), VerMaxUsingMin: " << maxver << "/" << maxver << "/" << minver << ", MsgType: 2(keepalive), Extensions: 0000";
	auto expected_str = ss.str ();

	// check expected vs real
	nano::keepalive keepalive_msg{ nano::dev::network_params.network };
	std::string header_string = keepalive_msg.header.to_string ();
	ASSERT_EQ (expected_str, header_string);
}

/**
 * Test that a confirm_ack can encode an empty hash set
 */
TEST (confirm_ack, empty_vote_hashes)
{
	nano::keypair key;
	auto vote = std::make_shared<nano::vote> (key.pub, key.prv, 0, 0, std::vector<nano::block_hash>{} /* empty */);
	nano::confirm_ack message{ nano::dev::network_params.network, vote };
}

TEST (message, bulk_pull_serialization)
{
	nano::bulk_pull message_in{ nano::dev::network_params.network };
	message_in.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		message_in.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };
	bool error = false;
	nano::message_header header{ error, stream };
	ASSERT_FALSE (error);
	nano::bulk_pull message_out{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_TRUE (header.bulk_pull_ascending ());
}

TEST (message, keepalive_to_string)
{
	nano::message_header hdr{ nano::dev::network_params.network, nano::message_type::keepalive };
	std::string expected = hdr.to_string ();

	nano::keepalive keepalive = nano::keepalive (nano::dev::network_params.network);
	ASSERT_EQ (keepalive.to_string (), expected + "\n:::0\n:::0\n:::0\n:::0\n:::0\n:::0\n:::0\n:::0");

	expected.append ("\n:::0");

	keepalive.peers[1] = nano::endpoint{ boost::asio::ip::make_address_v6 ("::1"), 45 };
	expected.append ("\n::1:45");

	keepalive.peers[2] = nano::endpoint{ boost::asio::ip::make_address_v6 ("2001:db8:85a3:8d3:1319:8a2e:370:7348"), 0 };
	expected.append ("\n2001:db8:85a3:8d3:1319:8a2e:370:7348:0");

	keepalive.peers[3] = nano::endpoint{ boost::asio::ip::make_address_v6 ("::"), 65535 };
	expected.append ("\n:::65535");

	for (int i = 4; i < keepalive.peers.size (); i++)
	{
		keepalive.peers[i] = nano::endpoint{ boost::asio::ip::make_address_v6 ("::ffff:1.2.3.4"), 1234 };
		expected.append ("\n::ffff:1.2.3.4:1234");
	}

	ASSERT_EQ (keepalive.to_string (), expected);
}
