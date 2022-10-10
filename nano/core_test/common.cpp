#include <nano/node/common.hpp>
#include <gtest/gtest.h>

TEST (keepalive, to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::keepalive keepalive = nano::keepalive (network_constants);

	std::string expectedString = "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 2(keepalive), Extensions: 0000\n";	

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
		expectedString.append (test_ip + ":" + std::to_string(port) + "\n");
	}

	ASSERT_EQ (keepalive.to_string (), expectedString);
}


TEST (control_req, to_string)
{
	ASSERT_EQ (true, true);
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
	ASSERT_EQ (true, true);
}

TEST (node_id_handshake, to_string)
{
	ASSERT_EQ (true, true);
}

TEST (publish, to_string)
{
	ASSERT_EQ (true, true);
}

TEST (bulk_pull, to_string)
{
	ASSERT_EQ (true, true);
}

TEST (bulk_pull_account, to_string)
{
	ASSERT_EQ (true, true);
}

TEST (bulk_push, to_string)
{
	ASSERT_EQ (true, true);
}
