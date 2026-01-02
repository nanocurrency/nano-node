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

auto nano::deserialize_message (
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
			if (!error && at_end (stream) || !message->block)
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
		default:
			return { nullptr, deserialize_message_status::invalid_message_type };
	}
	release_assert (false, "invalid message type");
}

nano::stat::detail nano::to_stat_detail (nano::deserialize_message_status status)
{
	return nano::enum_util::cast<nano::stat::detail> (status);
}

std::string_view nano::to_string (nano::deserialize_message_status status)
{
	return nano::enum_util::name (status);
}
