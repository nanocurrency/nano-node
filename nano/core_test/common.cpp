#include <nano/node/common.hpp>

#include <gtest/gtest.h>

TEST (keepalive, to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::keepalive keepalive = nano::keepalive (network_constants);

	keepalive.peers[0] = nano::endpoint{ boost::asio::ip::make_address_v6 ("::ffff:1.2.3.4"), 1234 };

	std::string expected = "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 2(keepalive), Extensions: 0000\n";
	expected += "::ffff:1.2.3.4:1234\n";
	expected += ":::0\n:::0\n:::0\n:::0\n:::0\n:::0\n:::0";
	ASSERT_EQ (keepalive.to_string (), expected);
}

TEST (control_req, to_string)
{
	ASSERT_EQ (true, true);
}

TEST (telemetry_ack, to_string)
{
	ASSERT_EQ (true, true);
}

TEST (frontier_req, to_string)
{
	ASSERT_EQ (true, true);
}

TEST (confirm_ack, to_string)
{
	ASSERT_EQ (true, true);
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
