#include <nano/node/common.hpp>

#include <gtest/gtest.h>

TEST (keepalive, to_string)
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

		int ip_length = test_ip.length();
		
		for (int i = 0; i < ip_length; i++)
			external_address_1[i] = test_ip[i];


		keepalive.peers[index] = nano::endpoint (boost::asio::ip::make_address_v6 (test_ip), port);
		expectedString.append ("\n" + test_ip + ":" + std::to_string (port));
	}

	ASSERT_EQ (keepalive.to_string (), expectedString);
}

TEST (confirm_req, to_string)
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
		expected_string.append ("\nPair: " + block_hash.to_string() + " | " + root.to_string());
	}

	ASSERT_EQ (confirm_req.to_string (), expected_string);
}

TEST (telemetry_ack, to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::telemetry_ack telemetry_ack = nano::telemetry_ack (network_constants);

	nano::telemetry_data telemetry_data = nano::telemetry_data (); 

	telemetry_ack.data = telemetry_data;

	ASSERT_EQ (telemetry_ack.to_string (), telemetry_data.to_string ());
}

TEST (frontier_req, to_string)
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

TEST (confirm_ack, to_string)
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

TEST (telemetry_req, to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::telemetry_req telemetry_req = nano::telemetry_req (network_constants);

	ASSERT_EQ (telemetry_req.to_string (), "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 12(telemetry_req), Extensions: 0000");
}

TEST (node_id_handshake, to_string_response_has_value)
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

TEST (node_id_handshake, to_string_response_has_no_value)
{
	nano::uint256_union query = 010010101101110101101;

	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::node_id_handshake node_id_handshake = nano::node_id_handshake (network_constants, query, boost::none);

	std::string expected_output = "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 10(node_id_handshake), Extensions: 0001\n";
	expected_output += "cookie=" + query.to_string ();

	ASSERT_EQ (node_id_handshake.to_string (), expected_output);
}

TEST (publish, to_string)
{
	nano::send_block block = nano::send_block ();
	std::shared_ptr block_ptr = std::make_shared<nano::send_block> (block);

	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::publish publish = nano::publish (network_constants, block_ptr);

	ASSERT_EQ (publish.to_string (), block_ptr->to_json());
}

TEST (bulk_pull, to_string)
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

TEST (bulk_pull_account, to_string_pending_hash_and_amount)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::bulk_pull_account bulk_pull_account = nano::bulk_pull_account (network_constants);

	nano::account account = nano::account ("12345678987654321");
	nano::amount minimum_amount = nano::amount(1234);
	nano::bulk_pull_account_flags flags = nano::bulk_pull_account_flags::pending_hash_and_amount;

	bulk_pull_account.account = account;
	bulk_pull_account.minimum_amount = minimum_amount;
	bulk_pull_account.flags = flags;

	std::string expected_string = account.to_string () + " min=" + minimum_amount.to_string () + " pend hash and amt";

	ASSERT_EQ (bulk_pull_account.to_string (), expected_string);
}

TEST (bulk_pull_account, to_string_pending_address_only)
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

TEST (bulk_pull_account, to_string_pending_hash_amount_and_address)
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

TEST (bulk_push, to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::bulk_push bulk_push = nano::bulk_push (network_constants);

	std::string expected_output = "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 7(bulk_push), Extensions: 0000";

	ASSERT_EQ (bulk_push.to_string (), expected_output);
}
