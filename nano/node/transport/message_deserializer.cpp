#include <nano/lib/enum_utils.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/message_deserializer.hpp>

nano::transport::message_deserializer::message_deserializer (nano::network_constants const & network_constants_a, nano::network_filter & publish_filter_a, nano::block_uniquer & block_uniquer_a, nano::vote_uniquer & vote_uniquer_a,
read_query read_op) :
	read_buffer{ std::make_shared<std::vector<uint8_t>> () },
	network_constants_m{ network_constants_a },
	publish_filter_m{ publish_filter_a },
	block_uniquer_m{ block_uniquer_a },
	vote_uniquer_m{ vote_uniquer_a },
	read_op{ std::move (read_op) }
{
	debug_assert (this->read_op);
	read_buffer->resize (MAX_MESSAGE_SIZE);
}

void nano::transport::message_deserializer::read (const nano::transport::message_deserializer::callback_type && callback)
{
	debug_assert (callback);
	debug_assert (read_op);

	status = parse_status::none;

	read_op (read_buffer, HEADER_SIZE, [this_l = shared_from_this (), callback = std::move (callback)] (boost::system::error_code const & ec, std::size_t size_a) {
		if (ec)
		{
			callback (ec, nullptr);
			return;
		}
		if (size_a != HEADER_SIZE)
		{
			callback (boost::asio::error::fault, nullptr);
			return;
		}
		this_l->received_header (std::move (callback));
	});
}

void nano::transport::message_deserializer::received_header (const nano::transport::message_deserializer::callback_type && callback)
{
	nano::bufferstream stream{ read_buffer->data (), HEADER_SIZE };
	auto error = false;
	nano::message_header header{ error, stream };
	if (error)
	{
		status = parse_status::invalid_header;
		callback (boost::asio::error::fault, nullptr);
		return;
	}
	if (header.network != network_constants_m.current_network)
	{
		status = parse_status::invalid_network;
		callback (boost::asio::error::fault, nullptr);
		return;
	}
	if (header.version_using < network_constants_m.protocol_version_min)
	{
		status = parse_status::outdated_version;
		callback (boost::asio::error::fault, nullptr);
		return;
	}
	if (!header.is_valid_message_type ())
	{
		status = parse_status::invalid_header;
		callback (boost::asio::error::fault, nullptr);
		return;
	}

	std::size_t payload_size = header.payload_length_bytes ();
	if (payload_size > MAX_MESSAGE_SIZE)
	{
		status = parse_status::message_size_too_big;
		callback (boost::asio::error::fault, nullptr);
		return;
	}
	debug_assert (payload_size <= read_buffer->capacity ());

	if (payload_size == 0)
	{
		// Payload size will be 0 for `bulk_push` & `telemetry_req` message type
		received_message (header, 0, std::move (callback));
	}
	else
	{
		debug_assert (read_op);
		read_op (read_buffer, payload_size, [this_l = shared_from_this (), payload_size, header, callback = std::move (callback)] (boost::system::error_code const & ec, std::size_t size_a) {
			if (ec)
			{
				callback (ec, nullptr);
				return;
			}
			if (size_a != payload_size)
			{
				callback (boost::asio::error::fault, nullptr);
				return;
			}
			this_l->received_message (header, size_a, std::move (callback));
		});
	}
}

void nano::transport::message_deserializer::received_message (nano::message_header header, std::size_t payload_size, const nano::transport::message_deserializer::callback_type && callback)
{
	auto message = deserialize (header, payload_size);
	if (message)
	{
		debug_assert (status == parse_status::none);
		status = parse_status::success;
		callback (boost::system::error_code{}, std::move (message));
	}
	else
	{
		debug_assert (status != parse_status::none);
		callback (boost::system::error_code{}, nullptr);
	}
}

std::unique_ptr<nano::message> nano::transport::message_deserializer::deserialize (nano::message_header header, std::size_t payload_size)
{
	release_assert (payload_size <= MAX_MESSAGE_SIZE);
	nano::bufferstream stream{ read_buffer->data (), payload_size };
	switch (header.type)
	{
		case nano::message_type::keepalive:
		{
			return deserialize_keepalive (stream, header);
		}
		case nano::message_type::publish:
		{
			// Early filtering to not waste time deserializing duplicate blocks
			nano::uint128_t digest;
			if (!publish_filter_m.apply (read_buffer->data (), payload_size, &digest))
			{
				return deserialize_publish (stream, header, digest);
			}
			else
			{
				status = parse_status::duplicate_publish_message;
			}
			break;
		}
		case nano::message_type::confirm_req:
		{
			return deserialize_confirm_req (stream, header);
		}
		case nano::message_type::confirm_ack:
		{
			return deserialize_confirm_ack (stream, header);
		}
		case nano::message_type::node_id_handshake:
		{
			return deserialize_node_id_handshake (stream, header);
		}
		case nano::message_type::telemetry_req:
		{
			return deserialize_telemetry_req (stream, header);
		}
		case nano::message_type::telemetry_ack:
		{
			return deserialize_telemetry_ack (stream, header);
		}
		case nano::message_type::bulk_pull:
		{
			return deserialize_bulk_pull (stream, header);
		}
		case nano::message_type::bulk_pull_account:
		{
			return deserialize_bulk_pull_account (stream, header);
		}
		case nano::message_type::bulk_push:
		{
			return deserialize_bulk_push (stream, header);
		}
		case nano::message_type::frontier_req:
		{
			return deserialize_frontier_req (stream, header);
		}
		case nano::message_type::asc_pull_req:
		{
			return deserialize_asc_pull_req (stream, header);
		}
		case nano::message_type::asc_pull_ack:
		{
			return deserialize_asc_pull_ack (stream, header);
		}
		default:
		{
			status = parse_status::invalid_message_type;
			break;
		}
	}
	return {};
}

std::unique_ptr<nano::keepalive> nano::transport::message_deserializer::deserialize_keepalive (nano::stream & stream, nano::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<nano::keepalive> (error, stream, header);
	if (!error && nano::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
	return {};
}

std::unique_ptr<nano::publish> nano::transport::message_deserializer::deserialize_publish (nano::stream & stream, nano::message_header const & header, nano::uint128_t const & digest_a)
{
	auto error = false;
	auto incoming = std::make_unique<nano::publish> (error, stream, header, digest_a, &block_uniquer_m);
	if (!error && nano::at_end (stream))
	{
		release_assert (incoming->block);
		if (!network_constants_m.work.validate_entry (*incoming->block))
		{
			return incoming;
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_publish_message;
	}
	return {};
}

std::unique_ptr<nano::confirm_req> nano::transport::message_deserializer::deserialize_confirm_req (nano::stream & stream, nano::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<nano::confirm_req> (error, stream, header);
	if (!error && nano::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_confirm_req_message;
	}
	return {};
}

std::unique_ptr<nano::confirm_ack> nano::transport::message_deserializer::deserialize_confirm_ack (nano::stream & stream, nano::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<nano::confirm_ack> (error, stream, header, &vote_uniquer_m);
	if (!error && nano::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
	return {};
}

std::unique_ptr<nano::node_id_handshake> nano::transport::message_deserializer::deserialize_node_id_handshake (nano::stream & stream, nano::message_header const & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::node_id_handshake> (error, stream, header);
	if (!error && nano::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
	return {};
}

std::unique_ptr<nano::telemetry_req> nano::transport::message_deserializer::deserialize_telemetry_req (nano::stream & stream, nano::message_header const & header)
{
	// Message does not use stream payload (header only)
	return std::make_unique<nano::telemetry_req> (header);
}

std::unique_ptr<nano::telemetry_ack> nano::transport::message_deserializer::deserialize_telemetry_ack (nano::stream & stream, nano::message_header const & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::telemetry_ack> (error, stream, header);
	// Intentionally not checking if at the end of stream, because these messages support backwards/forwards compatibility
	if (!error)
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_telemetry_ack_message;
	}
	return {};
}

std::unique_ptr<nano::bulk_pull> nano::transport::message_deserializer::deserialize_bulk_pull (nano::stream & stream, const nano::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::bulk_pull> (error, stream, header);
	if (!error && nano::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_bulk_pull_message;
	}
	return {};
}

std::unique_ptr<nano::bulk_pull_account> nano::transport::message_deserializer::deserialize_bulk_pull_account (nano::stream & stream, const nano::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::bulk_pull_account> (error, stream, header);
	if (!error && nano::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_bulk_pull_account_message;
	}
	return {};
}

std::unique_ptr<nano::frontier_req> nano::transport::message_deserializer::deserialize_frontier_req (nano::stream & stream, const nano::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::frontier_req> (error, stream, header);
	if (!error && nano::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_frontier_req_message;
	}
	return {};
}

std::unique_ptr<nano::bulk_push> nano::transport::message_deserializer::deserialize_bulk_push (nano::stream & stream, const nano::message_header & header)
{
	// Message does not use stream payload (header only)
	return std::make_unique<nano::bulk_push> (header);
}

std::unique_ptr<nano::asc_pull_req> nano::transport::message_deserializer::deserialize_asc_pull_req (nano::stream & stream, const nano::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::asc_pull_req> (error, stream, header);
	// Intentionally not checking if at the end of stream, because these messages support backwards/forwards compatibility
	if (!error)
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_asc_pull_req_message;
	}
	return {};
}

std::unique_ptr<nano::asc_pull_ack> nano::transport::message_deserializer::deserialize_asc_pull_ack (nano::stream & stream, const nano::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::asc_pull_ack> (error, stream, header);
	// Intentionally not checking if at the end of stream, because these messages support backwards/forwards compatibility
	if (!error)
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_asc_pull_ack_message;
	}
	return {};
}

/*
 *
 */

nano::stat::detail nano::transport::to_stat_detail (nano::transport::parse_status status)
{
	return nano::enum_util::cast<nano::stat::detail> (status);
}

std::string_view nano::transport::to_string (nano::transport::parse_status status)
{
	return nano::enum_util::name (status);
}
