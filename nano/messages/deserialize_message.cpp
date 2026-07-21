#include <nano/lib/block_type.hpp>
#include <nano/lib/constants.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/network_filter.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/work.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/messages/bulk_pull.hpp>
#include <nano/messages/bulk_pull_account.hpp>
#include <nano/messages/bulk_push.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/deserialize_message.hpp>
#include <nano/messages/frontier_req.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/messages/publish.hpp>
#include <nano/messages/telemetry.hpp>
#include <nano/messages/uniquers.hpp>
#include <nano/messages/vote_relay.hpp>

namespace
{
// Whether the block type names an actual block rather than a placeholder
bool is_block (nano::block_type type)
{
	switch (type)
	{
		case nano::block_type::send:
		case nano::block_type::receive:
		case nano::block_type::open:
		case nano::block_type::change:
		case nano::block_type::state:
			return true;
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
			break;
	}
	return false;
}
}

auto nano::deserialize_message_header (nano::buffer_view buffer, nano::network_constants const & network_constants) -> deserialize_message_header_result
{
	nano::bufferstream stream{ buffer.data (), buffer.size () };

	bool error = false;
	nano::messages::message_header header{ error, stream };
	if (error)
	{
		return { std::nullopt, deserialize_message_status::invalid_header };
	}
	if (!header.is_valid_message_type ())
	{
		return { std::nullopt, deserialize_message_status::invalid_message_type };
	}
	if (header.network != network_constants.current_network)
	{
		return { std::nullopt, deserialize_message_status::invalid_network };
	}
	if (header.version_using < network_constants.protocol_version_min)
	{
		return { std::nullopt, deserialize_message_status::outdated_version };
	}
	// The payload length of a publish follows from its block type, so a type without a block leaves the length undefined
	if (header.type == nano::messages::message_type::publish && !is_block (header.block_type ()))
	{
		return { std::nullopt, deserialize_message_status::invalid_header };
	}
	return { header, deserialize_message_status::success };
}

auto nano::deserialize_message_payload (
nano::buffer_view buffer,
nano::messages::message_header const & header,
nano::network_constants const & network_constants,
nano::network_filter * network_filter,
nano::block_uniquer * block_uniquer,
nano::vote_uniquer * vote_uniquer)
-> deserialize_message_result
{
	nano::bufferstream stream{ buffer.data (), buffer.size () };

	switch (header.type)
	{
		case nano::messages::message_type::keepalive:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::keepalive> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_keepalive_message };
		}
		break;
		case nano::messages::message_type::publish:
		{
			nano::uint128_t digest{ 0 };
			if (network_filter)
			{
				if (network_filter->apply (buffer.data (), buffer.size (), &digest))
				{
					return { nullptr, deserialize_message_status::duplicate_publish_message };
				}
			}

			bool error = false;
			auto message = std::make_unique<nano::messages::publish> (error, stream, header, digest, block_uniquer);
			if (!error && at_end (stream) && message->block)
			{
				if (!network_constants.work.validate_entry (*message->block))
				{
					return { std::move (message), deserialize_message_status::success };
				}
				else
				{
					return { nullptr, deserialize_message_status::insufficient_work };
				}
			}
			return { nullptr, deserialize_message_status::invalid_publish_message };
		}
		break;
		case nano::messages::message_type::confirm_req:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::confirm_req> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_confirm_req_message };
		}
		break;
		case nano::messages::message_type::confirm_ack:
		{
			nano::uint128_t digest{ 0 };
			if (network_filter)
			{
				if (network_filter->apply (buffer.data (), buffer.size (), &digest))
				{
					return { nullptr, deserialize_message_status::duplicate_confirm_ack_message };
				}
			}

			bool error = false;
			auto message = std::make_unique<nano::messages::confirm_ack> (error, stream, header, digest, vote_uniquer);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_confirm_ack_message };
		}
		break;
		case nano::messages::message_type::node_id_handshake:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::node_id_handshake> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_node_id_handshake_message };
		}
		break;
		case nano::messages::message_type::telemetry_req:
		{
			return { std::make_unique<nano::messages::telemetry_req> (header), deserialize_message_status::success };
		}
		break;
		case nano::messages::message_type::telemetry_ack:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::telemetry_ack> (error, stream, header);
			if (!error) // Intentionally not checking at_end here for forward compatibility
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_telemetry_ack_message };
		}
		break;
		case nano::messages::message_type::bulk_pull:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::bulk_pull> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_bulk_pull_message };
		}
		break;
		case nano::messages::message_type::bulk_pull_account:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::bulk_pull_account> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_bulk_pull_account_message };
		}
		break;
		case nano::messages::message_type::bulk_push:
		{
			return { std::make_unique<nano::messages::bulk_push> (header), deserialize_message_status::success };
		}
		break;
		case nano::messages::message_type::frontier_req:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::frontier_req> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_frontier_req_message };
		}
		break;
		case nano::messages::message_type::asc_pull_req:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::asc_pull_req> (error, stream, header);
			if (!error)
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_asc_pull_req_message };
		}
		break;
		case nano::messages::message_type::asc_pull_ack:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::asc_pull_ack> (error, stream, header);
			if (!error)
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_asc_pull_ack_message };
		}
		break;
		case nano::messages::message_type::vote_relay_req:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::vote_relay_req> (error, stream, header);
			if (!error) // Intentionally not checking at_end here for forward compatibility
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_vote_relay_req_message };
		}
		break;
		case nano::messages::message_type::vote_relay_ack:
		{
			bool error = false;
			auto message = std::make_unique<nano::messages::vote_relay_ack> (error, stream, header, vote_uniquer);
			if (!error) // Intentionally not checking at_end here for forward compatibility
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_vote_relay_ack_message };
		}
		break;
		case nano::messages::message_type::invalid:
		case nano::messages::message_type::not_a_type:
			break;
	}
	// No default case, so the compiler flags a message type missing above; placeholders and values outside the enum end up here
	return { nullptr, deserialize_message_status::invalid_message_type };
}

auto nano::deserialize_message (
nano::buffer_view buffer,
nano::network_constants const & network_constants,
nano::network_filter * network_filter,
nano::block_uniquer * block_uniquer,
nano::vote_uniquer * vote_uniquer)
-> deserialize_message_result
{
	auto [header, status] = deserialize_message_header (buffer, network_constants);
	if (!header)
	{
		return { nullptr, status };
	}

	// On a stream the declared length is what separates this message from the next, so a buffer holding one message must match it exactly
	auto const payload = buffer.subspan (nano::messages::message_header::size);
	if (payload.size () != header->payload_length_bytes ())
	{
		return { nullptr, deserialize_message_status::payload_length_mismatch };
	}

	return deserialize_message_payload (payload, *header, network_constants, network_filter, block_uniquer, vote_uniquer);
}

nano::stat::detail nano::to_stat_detail (nano::deserialize_message_status status)
{
	return nano::enum_convert<nano::stat::detail> (status);
}

std::string_view nano::to_string (nano::deserialize_message_status status)
{
	return nano::enum_to_string (status);
}
