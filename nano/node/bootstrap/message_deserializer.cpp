#include <nano/node/bootstrap/message_deserializer.hpp>
#include <nano/node/node.hpp>

nano::bootstrap::message_deserializer::message_deserializer (nano::network_constants const & network_constants, nano::network_filter & publish_filter, nano::block_uniquer & block_uniquer, nano::vote_uniquer & vote_uniquer) :
	read_buffer{ std::make_shared<std::vector<uint8_t>> () },
	network_constants{ network_constants },
	publish_filter{ publish_filter },
	block_uniquer{ block_uniquer },
	vote_uniquer{ vote_uniquer }
{
	read_buffer->resize (MAX_MESSAGE_SIZE);
}

void nano::bootstrap::message_deserializer::read (std::shared_ptr<nano::socket> socket, const nano::bootstrap::message_deserializer::callback_type && callback)
{
	debug_assert (callback);
	// Increase timeout to receive TCP header (idle server socket)
	socket->set_default_timeout_value (network_constants.idle_timeout);
	socket->async_read (read_buffer, HEADER_SIZE, [this_l = shared_from_this (), socket, callback = std::move (callback)] (boost::system::error_code const & ec, std::size_t size_a) {
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
		// TODO: Decrease timeout to default
		// Decrease timeout to default
		//		this_l->socket->set_default_timeout_value (this_l->node.config.tcp_io_timeout);
		this_l->received_header (socket, std::move (callback));
	});
}

void nano::bootstrap::message_deserializer::received_header (std::shared_ptr<nano::socket> socket, const nano::bootstrap::message_deserializer::callback_type && callback)
{
	nano::bufferstream stream{ read_buffer->data (), HEADER_SIZE };
	auto error = false;
	nano::message_header header{ error, stream };
	if (error)
	{
		status = parse_status::invalid_header;
		callback (boost::system::error_code{}, nullptr);
		return;
	}
	if (!header.is_valid_block_type ())
	{
		status = parse_status::invalid_header;
		callback (boost::system::error_code{}, nullptr);
		return;
	}
	if (header.network != network_constants.current_network)
	{
		status = parse_status::invalid_network;
		callback (boost::system::error_code{}, nullptr);
		return;
	}
	if (header.version_using < network_constants.protocol_version_min)
	{
		status = parse_status::outdated_version;
		callback (boost::system::error_code{}, nullptr);
		return;
	}

	std::size_t payload_size = header.payload_length_bytes ();
	if (payload_size >= MAX_MESSAGE_SIZE)
	{
		status = parse_status::message_size_too_big;
		callback (boost::system::error_code{}, nullptr);
		return;
	}
	debug_assert (payload_size <= read_buffer->capacity ());

	if (payload_size == 0)
	{
		received_message (header, 0, std::move (callback));
	}
	else
	{
		socket->async_read (read_buffer, payload_size, [this_l = shared_from_this (), payload_size, header, callback = std::move (callback)] (boost::system::error_code const & ec, std::size_t size_a) {
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

void nano::bootstrap::message_deserializer::received_message (nano::message_header header, std::size_t payload_size, const nano::bootstrap::message_deserializer::callback_type && callback)
{
	auto message = deserialize (header, payload_size);
	if (message)
	{
		callback (boost::system::error_code{}, std::move (message));
	}
	else
	{
		debug_assert (status != parse_status::success);
		callback (boost::system::error_code{}, nullptr);
	}
}

std::unique_ptr<nano::message> nano::bootstrap::message_deserializer::deserialize (nano::message_header header, std::size_t payload_size)
{
	release_assert (payload_size < MAX_MESSAGE_SIZE);
	nano::bufferstream stream{ read_buffer->data (), payload_size };
	switch (header.type)
	{
		case nano::message_type::keepalive:
		{
			return deserialize_keepalive (stream, header);
		}
		case nano::message_type::publish:
		{
			nano::uint128_t digest;
			if (!publish_filter.apply (read_buffer->data (), payload_size, &digest))
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
		default:
		{
			status = parse_status::invalid_message_type;
			break;
		}
	}
	return {};
}

std::unique_ptr<nano::keepalive> nano::bootstrap::message_deserializer::deserialize_keepalive (nano::stream & stream, nano::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<nano::keepalive> (error, stream, header);
	if (!error && at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
	return {};
}

std::unique_ptr<nano::publish> nano::bootstrap::message_deserializer::deserialize_publish (nano::stream & stream, nano::message_header const & header, nano::uint128_t const & digest_a)
{
	auto error = false;
	auto incoming = std::make_unique<nano::publish> (error, stream, header, digest_a, &block_uniquer);
	if (!error && at_end (stream))
	{
		release_assert (incoming->block);
		if (!network_constants.work.validate_entry (*incoming->block))
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

std::unique_ptr<nano::confirm_req> nano::bootstrap::message_deserializer::deserialize_confirm_req (nano::stream & stream, nano::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<nano::confirm_req> (error, stream, header, &block_uniquer);
	if (!error && at_end (stream))
	{
		if (incoming->block == nullptr || !network_constants.work.validate_entry (*incoming->block))
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
		status = parse_status::invalid_confirm_req_message;
	}
	return {};
}

std::unique_ptr<nano::confirm_ack> nano::bootstrap::message_deserializer::deserialize_confirm_ack (nano::stream & stream, nano::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<nano::confirm_ack> (error, stream, header, &vote_uniquer);
	if (!error && at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
	return {};
}

std::unique_ptr<nano::node_id_handshake> nano::bootstrap::message_deserializer::deserialize_node_id_handshake (nano::stream & stream, nano::message_header const & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::node_id_handshake> (error, stream, header);
	if (!error && at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
	return {};
}

std::unique_ptr<nano::telemetry_req> nano::bootstrap::message_deserializer::deserialize_telemetry_req (nano::stream & stream, nano::message_header const & header)
{
	auto incoming = std::make_unique<nano::telemetry_req> (header);
	if (at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_telemetry_req_message;
	}
	return {};
}

std::unique_ptr<nano::telemetry_ack> nano::bootstrap::message_deserializer::deserialize_telemetry_ack (nano::stream & stream, nano::message_header const & header)
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

std::unique_ptr<nano::bulk_pull> nano::bootstrap::message_deserializer::deserialize_bulk_pull (nano::stream & stream, const nano::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::bulk_pull> (error, stream, header);
	if (!error && at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_bulk_pull_message;
	}
	return {};
}

std::unique_ptr<nano::bulk_pull_account> nano::bootstrap::message_deserializer::deserialize_bulk_pull_account (nano::stream & stream, const nano::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::bulk_pull_account> (error, stream, header);
	if (!error && at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_bulk_pull_account_message;
	}
	return {};
}

std::unique_ptr<nano::frontier_req> nano::bootstrap::message_deserializer::deserialize_frontier_req (nano::stream & stream, const nano::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<nano::frontier_req> (error, stream, header);
	if (!error && at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_frontier_req_message;
	}
	return {};
}

std::unique_ptr<nano::bulk_push> nano::bootstrap::message_deserializer::deserialize_bulk_push (nano::stream & stream, const nano::message_header & header)
{
	return std::make_unique<nano::bulk_push> (header);
}

bool nano::bootstrap::message_deserializer::at_end (nano::stream & stream)
{
	uint8_t junk;
	auto end (nano::try_read (stream, junk));
	return end;
}

nano::stat::detail nano::bootstrap::message_deserializer::parse_status_to_stat_detail ()
{
	switch (status)
	{
		case parse_status::success:
			break;
		case parse_status::insufficient_work:
			return stat::detail::insufficient_work;
		case parse_status::invalid_header:
			return stat::detail::invalid_header;
		case parse_status::invalid_message_type:
			return stat::detail::invalid_message_type;
		case parse_status::invalid_keepalive_message:
			return stat::detail::invalid_keepalive_message;
		case parse_status::invalid_publish_message:
			return stat::detail::invalid_publish_message;
		case parse_status::invalid_confirm_req_message:
			return stat::detail::invalid_confirm_req_message;
		case parse_status::invalid_confirm_ack_message:
			return stat::detail::invalid_confirm_ack_message;
		case parse_status::invalid_node_id_handshake_message:
			return stat::detail::invalid_node_id_handshake_message;
		case parse_status::invalid_telemetry_req_message:
			return stat::detail::invalid_telemetry_req_message;
		case parse_status::invalid_telemetry_ack_message:
			return stat::detail::invalid_telemetry_ack_message;
		case parse_status::invalid_bulk_pull_message:
			return stat::detail::invalid_bulk_pull_message;
		case parse_status::invalid_bulk_pull_account_message:
			return stat::detail::invalid_bulk_pull_account_message;
		case parse_status::invalid_frontier_req_message:
			return stat::detail::invalid_frontier_req_message;
		case parse_status::invalid_network:
			return stat::detail::invalid_network;
		case parse_status::outdated_version:
			return stat::detail::outdated_version;
		case parse_status::duplicate_publish_message:
			return stat::detail::duplicate_publish;
		case parse_status::message_size_too_big:
			return stat::detail::message_too_big;
	}
	return {};
}

std::string nano::bootstrap::message_deserializer::parse_status_to_string ()
{
	switch (status)
	{
		case parse_status::success:
			return "success";
		case parse_status::insufficient_work:
			return "insufficient_work";
		case parse_status::invalid_header:
			return "invalid_header";
		case parse_status::invalid_message_type:
			return "invalid_message_type";
		case parse_status::invalid_keepalive_message:
			return "invalid_keepalive_message";
		case parse_status::invalid_publish_message:
			return "invalid_publish_message";
		case parse_status::invalid_confirm_req_message:
			return "invalid_confirm_req_message";
		case parse_status::invalid_confirm_ack_message:
			return "invalid_confirm_ack_message";
		case parse_status::invalid_node_id_handshake_message:
			return "invalid_node_id_handshake_message";
		case parse_status::invalid_telemetry_req_message:
			return "invalid_telemetry_req_message";
		case parse_status::invalid_telemetry_ack_message:
			return "invalid_telemetry_ack_message";
		case parse_status::invalid_bulk_pull_message:
			return "invalid_bulk_pull_message";
		case parse_status::invalid_bulk_pull_account_message:
			return "invalid_bulk_pull_account_message";
		case parse_status::invalid_frontier_req_message:
			return "invalid_frontier_req_message";
		case parse_status::invalid_network:
			return "invalid_network";
		case parse_status::outdated_version:
			return "outdated_version";
		case parse_status::duplicate_publish_message:
			return "duplicate_publish_message";
		case parse_status::message_size_too_big:
			return "message_size_too_big";
	}
	return "n/a";
}
