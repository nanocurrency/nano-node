#include <celerix/lib/enum_util.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/transport/message_deserializer.hpp>

celerix::transport::message_deserializer::message_deserializer (celerix::network_constants const & network_constants_a, celerix::network_filter & network_filter_a, celerix::block_uniquer & block_uniquer_a, celerix::vote_uniquer & vote_uniquer_a,
read_query read_op) :
	read_buffer{ std::make_shared<std::vector<uint8_t>> () },
	network_constants_m{ network_constants_a },
	network_filter_m{ network_filter_a },
	block_uniquer_m{ block_uniquer_a },
	vote_uniquer_m{ vote_uniquer_a },
	read_op{ std::move (read_op) }
{
	debug_assert (this->read_op);
	read_buffer->resize (MAX_MESSAGE_SIZE);
}

void celerix::transport::message_deserializer::read (const celerix::transport::message_deserializer::callback_type && callback)
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

void celerix::transport::message_deserializer::received_header (const celerix::transport::message_deserializer::callback_type && callback)
{
	celerix::bufferstream stream{ read_buffer->data (), HEADER_SIZE };
	auto error = false;
	celerix::message_header header{ error, stream };
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

void celerix::transport::message_deserializer::received_message (celerix::message_header header, std::size_t payload_size, const celerix::transport::message_deserializer::callback_type && callback)
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

std::unique_ptr<celerix::message> celerix::transport::message_deserializer::deserialize (celerix::message_header header, std::size_t payload_size)
{
	release_assert (payload_size <= MAX_MESSAGE_SIZE);
	celerix::bufferstream stream{ read_buffer->data (), payload_size };
	switch (header.type)
	{
		case celerix::message_type::keepalive:
		{
			return deserialize_keepalive (stream, header);
		}
		case celerix::message_type::publish:
		{
			// Early filtering to not waste time deserializing duplicates
			celerix::uint128_t digest;
			if (!network_filter_m.apply (read_buffer->data (), payload_size, &digest))
			{
				return deserialize_publish (stream, header, digest);
			}
			else
			{
				status = parse_status::duplicate_publish_message;
			}
			break;
		}
		case celerix::message_type::confirm_req:
		{
			return deserialize_confirm_req (stream, header);
		}
		case celerix::message_type::confirm_ack:
		{
			// Early filtering to not waste time deserializing duplicates
			celerix::uint128_t digest;
			if (!network_filter_m.apply (read_buffer->data (), payload_size, &digest))
			{
				return deserialize_confirm_ack (stream, header, digest);
			}
			else
			{
				status = parse_status::duplicate_confirm_ack_message;
			}
			break;
		}
		case celerix::message_type::node_id_handshake:
		{
			return deserialize_node_id_handshake (stream, header);
		}
		case celerix::message_type::telemetry_req:
		{
			return deserialize_telemetry_req (stream, header);
		}
		case celerix::message_type::telemetry_ack:
		{
			return deserialize_telemetry_ack (stream, header);
		}
		case celerix::message_type::bulk_pull:
		{
			return deserialize_bulk_pull (stream, header);
		}
		case celerix::message_type::bulk_pull_account:
		{
			return deserialize_bulk_pull_account (stream, header);
		}
		case celerix::message_type::bulk_push:
		{
			return deserialize_bulk_push (stream, header);
		}
		case celerix::message_type::frontier_req:
		{
			return deserialize_frontier_req (stream, header);
		}
		case celerix::message_type::asc_pull_req:
		{
			return deserialize_asc_pull_req (stream, header);
		}
		case celerix::message_type::asc_pull_ack:
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

std::unique_ptr<celerix::keepalive> celerix::transport::message_deserializer::deserialize_keepalive (celerix::stream & stream, celerix::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<celerix::keepalive> (error, stream, header);
	if (!error && celerix::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
	return {};
}

std::unique_ptr<celerix::publish> celerix::transport::message_deserializer::deserialize_publish (celerix::stream & stream, celerix::message_header const & header, celerix::network_filter::digest_t const & digest_a)
{
	auto error = false;
	auto incoming = std::make_unique<celerix::publish> (error, stream, header, digest_a, &block_uniquer_m);
	if (!error && celerix::at_end (stream))
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

std::unique_ptr<celerix::confirm_req> celerix::transport::message_deserializer::deserialize_confirm_req (celerix::stream & stream, celerix::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<celerix::confirm_req> (error, stream, header);
	if (!error && celerix::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_confirm_req_message;
	}
	return {};
}

std::unique_ptr<celerix::confirm_ack> celerix::transport::message_deserializer::deserialize_confirm_ack (celerix::stream & stream, celerix::message_header const & header, celerix::network_filter::digest_t const & digest_a)
{
	auto error = false;
	auto incoming = std::make_unique<celerix::confirm_ack> (error, stream, header, digest_a, &vote_uniquer_m);
	if (!error && celerix::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
	return {};
}

std::unique_ptr<celerix::node_id_handshake> celerix::transport::message_deserializer::deserialize_node_id_handshake (celerix::stream & stream, celerix::message_header const & header)
{
	bool error = false;
	auto incoming = std::make_unique<celerix::node_id_handshake> (error, stream, header);
	if (!error && celerix::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
	return {};
}

std::unique_ptr<celerix::telemetry_req> celerix::transport::message_deserializer::deserialize_telemetry_req (celerix::stream & stream, celerix::message_header const & header)
{
	// Message does not use stream payload (header only)
	return std::make_unique<celerix::telemetry_req> (header);
}

std::unique_ptr<celerix::telemetry_ack> celerix::transport::message_deserializer::deserialize_telemetry_ack (celerix::stream & stream, celerix::message_header const & header)
{
	bool error = false;
	auto incoming = std::make_unique<celerix::telemetry_ack> (error, stream, header);
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

std::unique_ptr<celerix::bulk_pull> celerix::transport::message_deserializer::deserialize_bulk_pull (celerix::stream & stream, const celerix::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<celerix::bulk_pull> (error, stream, header);
	if (!error && celerix::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_bulk_pull_message;
	}
	return {};
}

std::unique_ptr<celerix::bulk_pull_account> celerix::transport::message_deserializer::deserialize_bulk_pull_account (celerix::stream & stream, const celerix::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<celerix::bulk_pull_account> (error, stream, header);
	if (!error && celerix::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_bulk_pull_account_message;
	}
	return {};
}

std::unique_ptr<celerix::frontier_req> celerix::transport::message_deserializer::deserialize_frontier_req (celerix::stream & stream, const celerix::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<celerix::frontier_req> (error, stream, header);
	if (!error && celerix::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_frontier_req_message;
	}
	return {};
}

std::unique_ptr<celerix::bulk_push> celerix::transport::message_deserializer::deserialize_bulk_push (celerix::stream & stream, const celerix::message_header & header)
{
	// Message does not use stream payload (header only)
	return std::make_unique<celerix::bulk_push> (header);
}

std::unique_ptr<celerix::asc_pull_req> celerix::transport::message_deserializer::deserialize_asc_pull_req (celerix::stream & stream, const celerix::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<celerix::asc_pull_req> (error, stream, header);
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

std::unique_ptr<celerix::asc_pull_ack> celerix::transport::message_deserializer::deserialize_asc_pull_ack (celerix::stream & stream, const celerix::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<celerix::asc_pull_ack> (error, stream, header);
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

celerix::stat::detail celerix::transport::to_stat_detail (celerix::transport::parse_status status)
{
	return celerix::enum_util::cast<celerix::stat::detail> (status);
}

std::string_view celerix::transport::to_string (celerix::transport::parse_status status)
{
	return celerix::enum_util::name (status);
}
