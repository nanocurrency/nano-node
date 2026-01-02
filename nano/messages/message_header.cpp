#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/constants.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/messages/bulk_pull.hpp>
#include <nano/messages/bulk_pull_account.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/frontier_req.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/messages/message_header.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/messages/telemetry.hpp>

#include <boost/endian/conversion.hpp>

namespace nano::messages
{
message_header::message_header (nano::network_constants const & constants, message_type type_a) :
	network{ constants.current_network },
	version_max{ constants.protocol_version },
	version_using{ constants.protocol_version },
	version_min{ constants.protocol_version_min },
	type (type_a)
{
}

message_header::message_header (bool & error_a, nano::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void message_header::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, boost::endian::native_to_big (static_cast<uint16_t> (network)));
	nano::write (stream_a, version_max);
	nano::write (stream_a, version_using);
	nano::write (stream_a, version_min);
	nano::write (stream_a, type);
	nano::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
}

bool message_header::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		uint16_t network_bytes;
		nano::read (stream_a, network_bytes);
		network = static_cast<nano::network_type> (boost::endian::big_to_native (network_bytes));
		nano::read (stream_a, version_max);
		nano::read (stream_a, version_using);
		nano::read (stream_a, version_min);
		nano::read (stream_a, type);
		uint16_t extensions_l;
		nano::read (stream_a, extensions_l);
		extensions = extensions_l;
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

nano::block_type message_header::block_type () const
{
	return static_cast<nano::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void message_header::block_type_set (nano::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= (extensions_bitset_t{ static_cast<unsigned long long> (type_a) } << 8);
}

uint8_t message_header::count_get () const
{
	debug_assert (type == message_type::confirm_ack || type == message_type::confirm_req);
	debug_assert (!flag_test (confirm_v2_flag)); // Only valid for v1

	return static_cast<uint8_t> (((extensions & count_mask) >> 12).to_ullong ());
}

void message_header::count_set (uint8_t count_a)
{
	debug_assert (type == message_type::confirm_ack || type == message_type::confirm_req);
	debug_assert (!flag_test (confirm_v2_flag)); // Only valid for v1
	debug_assert (count_a < 16); // Max 4 bits

	extensions &= ~count_mask;
	extensions |= ((extensions_bitset_t{ count_a } << 12) & count_mask);
}

/*
 * We need those shenanigans because we need to keep compatibility with previous protocol versions (<= V25.1)
 */

uint8_t message_header::count_v2_get () const
{
	debug_assert (type == message_type::confirm_ack || type == message_type::confirm_req);
	debug_assert (flag_test (confirm_v2_flag)); // Only valid for v2

	// Extract 2 parts of 4 bits
	auto left = (extensions & count_v2_mask_left) >> 12;
	auto right = (extensions & count_v2_mask_right) >> 4;

	return static_cast<uint8_t> (((left << 4) | right).to_ullong ());
}

void message_header::count_v2_set (uint8_t count)
{
	debug_assert (type == message_type::confirm_ack || type == message_type::confirm_req);
	debug_assert (flag_test (confirm_v2_flag)); // Only valid for v2

	extensions &= ~(count_v2_mask_left | count_v2_mask_right);

	// Split count into 2 parts of 4 bits
	extensions_bitset_t trim_mask{ 0xf };
	auto left = (extensions_bitset_t{ count } >> 4) & trim_mask;
	auto right = (extensions_bitset_t{ count }) & trim_mask;

	extensions |= (left << 12) | (right << 4);
}

bool message_header::flag_test (uint8_t flag) const
{
	// Extension bits at index >= 8 are block type & count
	debug_assert (flag < 8);
	return extensions.test (flag);
}

void message_header::flag_set (uint8_t flag, bool enable)
{
	// Extension bits at index >= 8 are block type & count
	debug_assert (flag < 8);
	extensions.set (flag, enable);
}

bool message_header::bulk_pull_is_count_present () const
{
	auto result (false);
	if (type == message_type::bulk_pull)
	{
		if (extensions.test (bulk_pull_count_present_flag))
		{
			result = true;
		}
	}
	return result;
}

bool message_header::bulk_pull_ascending () const
{
	auto result (false);
	if (type == message_type::bulk_pull)
	{
		if (extensions.test (bulk_pull_ascending_flag))
		{
			result = true;
		}
	}
	return result;
}

bool message_header::frontier_req_is_only_confirmed_present () const
{
	auto result (false);
	if (type == message_type::frontier_req)
	{
		if (extensions.test (frontier_req_only_confirmed))
		{
			result = true;
		}
	}
	return result;
}

bool message_header::confirm_is_v2 () const
{
	debug_assert (type == message_type::confirm_ack || type == message_type::confirm_req);
	return flag_test (confirm_v2_flag);
}

void message_header::confirm_set_v2 (bool value)
{
	debug_assert (type == message_type::confirm_ack || type == message_type::confirm_req);
	flag_set (confirm_v2_flag, value);
}

std::size_t message_header::payload_length_bytes () const
{
	switch (type)
	{
		case message_type::bulk_pull:
		{
			return bulk_pull::size + (bulk_pull_is_count_present () ? bulk_pull::extended_parameters_size : 0);
		}
		case message_type::bulk_push:
		case message_type::telemetry_req:
		{
			// These don't have a payload
			return 0;
		}
		case message_type::frontier_req:
		{
			return frontier_req::size;
		}
		case message_type::bulk_pull_account:
		{
			return bulk_pull_account::size;
		}
		case message_type::keepalive:
		{
			return keepalive::size;
		}
		case message_type::publish:
		{
			return nano::block::size (block_type ());
		}
		case message_type::confirm_ack:
		{
			return confirm_ack::size (*this);
		}
		case message_type::confirm_req:
		{
			return confirm_req::size (*this);
		}
		case message_type::node_id_handshake:
		{
			return node_id_handshake::size (*this);
		}
		case message_type::telemetry_ack:
		{
			return telemetry_ack::size (*this);
		}
		case message_type::asc_pull_req:
		{
			return asc_pull_req::size (*this);
		}
		case message_type::asc_pull_ack:
		{
			return asc_pull_ack::size (*this);
		}
		default:
		{
			debug_assert (false);
			return 0;
		}
	}
}

bool message_header::is_valid_message_type () const
{
	switch (type)
	{
		case message_type::bulk_pull:
		case message_type::bulk_push:
		case message_type::telemetry_req:
		case message_type::frontier_req:
		case message_type::bulk_pull_account:
		case message_type::keepalive:
		case message_type::publish:
		case message_type::confirm_ack:
		case message_type::confirm_req:
		case message_type::node_id_handshake:
		case message_type::telemetry_ack:
		case message_type::asc_pull_req:
		case message_type::asc_pull_ack:
		{
			return true;
		}
		default:
		{
			return false;
		}
	}
}

void message_header::operator() (nano::object_stream & obs) const
{
	obs.write ("type", type);
	obs.write ("network", to_string (network));
	obs.write ("network_raw", static_cast<uint16_t> (network));
	obs.write ("version", static_cast<uint16_t> (version_using));
	obs.write ("version_min", static_cast<uint16_t> (version_min));
	obs.write ("version_max", static_cast<uint16_t> (version_max));
	obs.write ("extensions", static_cast<uint16_t> (extensions.to_ulong ()));
}
}
