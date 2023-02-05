#include <nano/node/common.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/node.hpp>
#include <nano/test_common/telemetry.hpp>

#include <gtest/gtest.h>

namespace
{
void compare_telemetry_data_impl (const nano::telemetry_data & data_a, const nano::telemetry_data & data_b, bool & result)
{
	ASSERT_EQ (data_a.block_count, data_b.block_count);
	ASSERT_EQ (data_a.cemented_count, data_b.cemented_count);
	ASSERT_EQ (data_a.bandwidth_cap, data_b.bandwidth_cap);
	ASSERT_EQ (data_a.peer_count, data_b.peer_count);
	ASSERT_EQ (data_a.protocol_version, data_b.protocol_version);
	ASSERT_EQ (data_a.unchecked_count, data_b.unchecked_count);
	ASSERT_EQ (data_a.account_count, data_b.account_count);
	ASSERT_LE (data_a.uptime, data_b.uptime);
	ASSERT_EQ (data_a.genesis_block, data_b.genesis_block);
	ASSERT_EQ (data_a.major_version, nano::get_major_node_version ());
	ASSERT_EQ (data_a.minor_version, nano::get_minor_node_version ());
	ASSERT_EQ (data_a.patch_version, nano::get_patch_node_version ());
	ASSERT_EQ (data_a.pre_release_version, nano::get_pre_release_node_version ());
	ASSERT_EQ (data_a.maker, static_cast<std::underlying_type_t<nano::telemetry_maker>> (nano::telemetry_maker::nf_node));
	ASSERT_GT (data_a.timestamp, std::chrono::system_clock::now () - std::chrono::seconds (100));
	ASSERT_EQ (data_a.active_difficulty, data_b.active_difficulty);
	ASSERT_EQ (data_a.unknown_data, std::vector<uint8_t>{});
	result = true;
}
}

bool nano::test::compare_telemetry_data (const nano::telemetry_data & data_a, const nano::telemetry_data & data_b)
{
	bool result = false;
	compare_telemetry_data_impl (data_a, data_b, result);
	return result;
}

namespace
{
void compare_telemetry_impl (const nano::telemetry_data & data, nano::node const & node, bool & result)
{
	ASSERT_FALSE (data.validate_signature ());
	ASSERT_EQ (data.node_id, node.node_id.pub);

	// Signature should be different because uptime/timestamp will have changed.
	nano::telemetry_data data_l = data;
	data_l.signature.clear ();
	data_l.sign (node.node_id);
	ASSERT_NE (data.signature, data_l.signature);

	ASSERT_TRUE (nano::test::compare_telemetry_data (data, node.local_telemetry ()));

	result = true;
}
}

bool nano::test::compare_telemetry (const nano::telemetry_data & data, const nano::node & node)
{
	bool result = false;
	compare_telemetry_impl (data, node, result);
	return result;
}