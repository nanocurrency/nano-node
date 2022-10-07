#include <nano/node/common.hpp>
#include <gtest/gtest.h>

TEST (keepalive, to_string)
{
	nano::work_thresholds work_threshold = nano::work_thresholds (0, 0, 0);
	nano::network_constants network_constants = nano::network_constants (work_threshold, nano::networks::nano_dev_network);
	nano::keepalive keepalive = nano::keepalive (network_constants);

	ASSERT_EQ (keepalive.to_string (), "NetID: 5241(dev), VerMaxUsingMin: 19/19/18, MsgType: 2(keepalive), Extensions: 0000\n:::0\n:::0\n:::0\n:::0\n:::0\n:::0\n:::0\n:::0\n");
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
