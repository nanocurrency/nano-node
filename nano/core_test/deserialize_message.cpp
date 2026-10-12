#include <nano/lib/block_type.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/stream.hpp>
#include <nano/messages/deserialize_message.hpp>
#include <nano/messages/message_header.hpp>
#include <nano/secure/network_params.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{
std::vector<uint8_t> serialize_header (nano::messages::message_header const & header)
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		header.serialize (stream);
	}
	return bytes;
}
}

/*
 * Every message type the protocol defines passes header validation and parses back to the bytes it was serialized from,
 * while the two placeholder values are rejected
 */
TEST (deserialize_message_header, message_types)
{
	auto const & network = nano::dev::network_params.network;
	for (auto type : nano::enum_values<nano::messages::message_type> ())
	{
		SCOPED_TRACE (nano::messages::to_string (type));
		nano::messages::message_header header{ network, type };
		header.block_type_set (nano::block_type::state); // Publish derives its payload length from the block type
		auto const bytes = serialize_header (header);

		auto [parsed, status] = nano::deserialize_message_header (bytes, network);
		if (type == nano::messages::message_type::invalid || type == nano::messages::message_type::not_a_type)
		{
			ASSERT_FALSE (parsed);
			ASSERT_EQ (status, nano::deserialize_message_status::invalid_message_type);
		}
		else
		{
			ASSERT_TRUE (parsed);
			ASSERT_EQ (status, nano::deserialize_message_status::success);
			ASSERT_EQ (serialize_header (*parsed), bytes);
		}
	}
}

/*
 * Type values outside the enum, such as a retired type or one from a newer protocol, are rejected
 */
TEST (deserialize_message_header, unknown_message_type)
{
	auto const & network = nano::dev::network_params.network;
	auto const last_type = nano::enum_values<nano::messages::message_type> ().back ();
	std::vector<uint8_t> const unknown_types{ 0x09, static_cast<uint8_t> (static_cast<uint8_t> (last_type) + 1), 0xff };
	for (auto value : unknown_types)
	{
		SCOPED_TRACE (static_cast<int> (value));
		nano::messages::message_header header{ network, static_cast<nano::messages::message_type> (value) };

		auto [parsed, status] = nano::deserialize_message_header (serialize_header (header), network);
		ASSERT_FALSE (parsed);
		ASSERT_EQ (status, nano::deserialize_message_status::invalid_message_type);
	}
}

/*
 * A buffer shorter than a header is rejected at every length
 */
TEST (deserialize_message_header, short_buffer)
{
	auto const & network = nano::dev::network_params.network;
	auto const bytes = serialize_header (nano::messages::message_header{ network, nano::messages::message_type::keepalive });
	ASSERT_EQ (bytes.size (), nano::messages::message_header::size);
	for (std::size_t size = 0; size < bytes.size (); ++size)
	{
		SCOPED_TRACE (size);
		auto [parsed, status] = nano::deserialize_message_header (nano::buffer_view{ bytes.data (), size }, network);
		ASSERT_FALSE (parsed);
		ASSERT_EQ (status, nano::deserialize_message_status::invalid_header);
	}
}

/*
 * A header from another network is rejected
 */
TEST (deserialize_message_header, invalid_network)
{
	auto const & network = nano::dev::network_params.network;
	nano::messages::message_header header{ network, nano::messages::message_type::keepalive };
	header.network = nano::network_type::nano_live_network;
	ASSERT_NE (header.network, network.current_network);

	auto [parsed, status] = nano::deserialize_message_header (serialize_header (header), network);
	ASSERT_FALSE (parsed);
	ASSERT_EQ (status, nano::deserialize_message_status::invalid_network);
}

/*
 * The protocol version in use must be at least the minimum this node supports, newer versions are accepted
 */
TEST (deserialize_message_header, protocol_version)
{
	auto const & network = nano::dev::network_params.network;
	auto parse_version = [&network] (uint8_t version_using) {
		nano::messages::message_header header{ network, nano::messages::message_type::keepalive };
		header.version_using = version_using;
		return nano::deserialize_message_header (serialize_header (header), network);
	};

	auto [outdated, outdated_status] = parse_version (network.protocol_version_min - 1);
	ASSERT_FALSE (outdated);
	ASSERT_EQ (outdated_status, nano::deserialize_message_status::outdated_version);

	auto [oldest, oldest_status] = parse_version (network.protocol_version_min);
	ASSERT_TRUE (oldest);
	ASSERT_EQ (oldest_status, nano::deserialize_message_status::success);

	auto [newer, newer_status] = parse_version (network.protocol_version + 1);
	ASSERT_TRUE (newer);
	ASSERT_EQ (newer_status, nano::deserialize_message_status::success);
}
