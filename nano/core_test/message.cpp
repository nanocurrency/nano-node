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
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::keepalive keepalive = nano::keepalive (network_constants);

	std::string expectedString = "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 2(keepalive), Extensions: 0000";

	for (auto peer = keepalive.peers.begin (), peers_end = keepalive.peers.end (); peer != peers_end; ++peer)
	{
		int index = std::distance (keepalive.peers.begin (), peer);

		std::string test_ip = "::ffff:1.2.3.4";
		int port = 7072;
		std::array<char, 64> external_address_1 = {};

		int ip_length = test_ip.length ();

		for (int i = 0; i < ip_length; i++)
			external_address_1[i] = test_ip[i];

		keepalive.peers[index] = nano::endpoint (boost::asio::ip::make_address_v6 (test_ip), port);
		expectedString.append ("\n" + test_ip + ":" + std::to_string (port));
	}

	ASSERT_EQ (keepalive.to_string (), expectedString);
}

TEST (message, confirm_req_to_string)
{
	nano::send_block block = nano::send_block ();
	std::shared_ptr block_ptr = std::make_shared<nano::send_block> (block);

	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::confirm_req confirm_req = nano::confirm_req (network_constants, block_ptr);

	std::string expected_string = "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 4(confirm_req), Extensions: 0200";

	for (auto roots_hash = 0, roots_hash_end = 3; roots_hash != roots_hash_end; ++roots_hash)
	{
		nano::block_hash block_hash = nano::block_hash ("123456786987654321");
		nano::root root = nano::root (10);

		confirm_req.roots_hashes.push_back (std::pair (block_hash, root));
		expected_string.append ("\nPair: " + block_hash.to_string () + " | " + root.to_string ());
	}

	ASSERT_EQ (confirm_req.to_string (), expected_string);
}

TEST (message, telemetry_ack_to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::telemetry_ack telemetry_ack = nano::telemetry_ack (network_constants);

	nano::telemetry_data telemetry_data = nano::telemetry_data ();

	telemetry_ack.data = telemetry_data;

	ASSERT_EQ (telemetry_ack.to_string (), telemetry_data.to_string ());
}

TEST (message, frontier_req_to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::frontier_req frontier_req = nano::frontier_req (network_constants);

	nano::account start = nano::account (12345678987564312);
	uint32_t age = 99999999;
	uint32_t count = 1234;

	frontier_req.start = start;
	frontier_req.age = age;
	frontier_req.count = count;

	std::string expected_output = start.to_string () + " maxage=" + std::to_string (age) + " count=" + std::to_string (count);

	ASSERT_EQ (frontier_req.to_string (), expected_output);
}

TEST (message, confirm_ack_to_string)
{
	nano::vote vote = nano::vote ();
	nano::account start = nano::account (12345678987564312);
	vote.account = start;

	std::shared_ptr vote_ptr = std::make_shared<nano::vote> (vote);

	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::confirm_ack confirm_ack = nano::confirm_ack (network_constants, vote_ptr);

	ASSERT_EQ (confirm_ack.to_string (), vote_ptr->account.to_string ());
}

TEST (message, telemetry_req_to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::telemetry_req telemetry_req = nano::telemetry_req (network_constants);

	ASSERT_EQ (telemetry_req.to_string (), "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 12(telemetry_req), Extensions: 0000");
}

TEST (message, node_id_handshake_to_string_response_has_value)
{
	nano::uint256_union query = 010010101101110101101;

	nano::account account = nano::account ("123456789875654321");
	nano::signature signature = nano::signature (0);

	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::node_id_handshake node_id_handshake = nano::node_id_handshake (network_constants, query, std::pair (account, signature));

	std::string expected_output = "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 10(node_id_handshake), Extensions: 0003\n";
	expected_output += "cookie=" + query.to_string ();
	expected_output += account.to_string () + " ";
	expected_output += signature.to_string ();

	ASSERT_EQ (node_id_handshake.to_string (), expected_output);
}

TEST (message, node_id_handshake_to_string_response_has_no_value)
{
	nano::uint256_union query = 010010101101110101101;

	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::node_id_handshake node_id_handshake = nano::node_id_handshake (network_constants, query, boost::none);

	std::string expected_output = "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 10(node_id_handshake), Extensions: 0001\n";
	expected_output += "cookie=" + query.to_string ();

	ASSERT_EQ (node_id_handshake.to_string (), expected_output);
}

TEST (message, publish_to_string)
{
	nano::send_block block = nano::send_block ();
	std::shared_ptr block_ptr = std::make_shared<nano::send_block> (block);

	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::publish publish = nano::publish (network_constants, block_ptr);

	ASSERT_EQ (publish.to_string (), block_ptr->to_json ());
}

TEST (message, bulk_pull_to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::bulk_pull bulk_pull = nano::bulk_pull (network_constants);

	nano::hash_or_account start = nano::account ("12345678987654321");
	nano::block_hash end = nano::block_hash ();
	uint32_t count = 3;

	bulk_pull.start = start;
	bulk_pull.end = end;
	bulk_pull.count = count;

	std::string expected_string = start.to_string () + " endhash=" + end.to_string () + " count=" + std::to_string (count);

	ASSERT_EQ (bulk_pull.to_string (), expected_string);
}

TEST (message, bulk_pull_account_to_string_pending_hash_and_amount)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::bulk_pull_account bulk_pull_account = nano::bulk_pull_account (network_constants);

	nano::account account = nano::account ("12345678987654321");
	nano::amount minimum_amount = nano::amount (1234);
	nano::bulk_pull_account_flags flags = nano::bulk_pull_account_flags::pending_hash_and_amount;

	bulk_pull_account.account = account;
	bulk_pull_account.minimum_amount = minimum_amount;
	bulk_pull_account.flags = flags;

	std::string expected_string = account.to_string () + " min=" + minimum_amount.to_string () + " pend hash and amt";

	ASSERT_EQ (bulk_pull_account.to_string (), expected_string);
}

TEST (message, bulk_pull_account_to_string_pending_address_only)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::bulk_pull_account bulk_pull_account = nano::bulk_pull_account (network_constants);

	nano::account account = nano::account ("12345678987654321");
	nano::amount minimum_amount = nano::amount (1234);
	nano::bulk_pull_account_flags flags = nano::bulk_pull_account_flags::pending_address_only;

	bulk_pull_account.account = account;
	bulk_pull_account.minimum_amount = minimum_amount;
	bulk_pull_account.flags = flags;

	std::string expected_string = account.to_string () + " min=" + minimum_amount.to_string () + " pend addr";

	ASSERT_EQ (bulk_pull_account.to_string (), expected_string);
}

TEST (message, bulk_pull_account_to_string_pending_hash_amount_and_address)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::bulk_pull_account bulk_pull_account = nano::bulk_pull_account (network_constants);

	nano::account account = nano::account ("12345678987654321");
	nano::amount minimum_amount = nano::amount (1234);
	nano::bulk_pull_account_flags flags = nano::bulk_pull_account_flags::pending_hash_amount_and_address;

	bulk_pull_account.account = account;
	bulk_pull_account.minimum_amount = minimum_amount;
	bulk_pull_account.flags = flags;

	std::string expected_string = account.to_string () + " min=" + minimum_amount.to_string () + " pend hash amt and addr";

	ASSERT_EQ (bulk_pull_account.to_string (), expected_string);
}

TEST (message, bulk_push_to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::bulk_push bulk_push = nano::bulk_push (network_constants);

	std::string expected_output = "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 7(bulk_push), Extensions: 0000";

	ASSERT_EQ (bulk_push.to_string (), expected_output);
}
