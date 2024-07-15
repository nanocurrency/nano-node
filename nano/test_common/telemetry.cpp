#include <nano/node/common.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/node.hpp>
#include <nano/test_common/telemetry.hpp>

#include <gtest/gtest.h>

namespace
{
void compare_telemetry_impl (nano::telemetry_data data, nano::node const & node, bool & result)
{
	ASSERT_FALSE (data.validate_signature ());
	ASSERT_EQ (data.node_id, node.node_id.pub);

	auto data_node = node.local_telemetry ();

	// Ignore timestamps and uptime
	data.timestamp = data_node.timestamp = {};
	data.uptime = data_node.uptime = {};

	// Signature should be different because uptime/timestamp will have changed.
	nano::telemetry_data data_l = data;
	data_l.signature.clear ();
	data_l.sign (node.node_id);
	ASSERT_NE (data.signature, data_l.signature);

	// Clear signatures for comparison
	data.signature.clear ();
	data_node.signature.clear ();

	ASSERT_EQ (data, data_node);

	result = true;
}
}

bool nano::test::compare_telemetry (nano::telemetry_data data, const nano::node & node)
{
	bool result = false;
	compare_telemetry_impl (data, node, result);
	return result;
}