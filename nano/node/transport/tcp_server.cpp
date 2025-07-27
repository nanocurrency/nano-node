#include <nano/node/messages.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/message_deserializer.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_server.hpp>

#include <memory>

nano::transport::tcp_server::tcp_server (nano::node & node_a, std::shared_ptr<nano::transport::tcp_socket> socket_a) :
	node{ node_a },
	socket{ socket_a },
	strand{ node_a.io_ctx.get_executor () },
	task{ strand },
	buffer{ std::make_shared<nano::shared_buffer::element_type> (max_buffer_size) }
{
}

nano::transport::tcp_server::~tcp_server ()
{
	close ();
}

void nano::transport::tcp_server::close ()
{
	stop ();
	socket->close ();
}

void nano::transport::tcp_server::close_async ()
{
	socket->close_async ();
}

// Starting the server must be separate from the constructor to allow the socket to access shared_from_this
void nano::transport::tcp_server::start ()
{
	task = nano::async::task (strand, start_impl ());
}

void nano::transport::tcp_server::stop ()
{
	if (task.running ())
	{
		// Node context must be running to gracefully stop async tasks
		debug_assert (!node.io_ctx.stopped ());
		// Ensure that we are not trying to await the task while running on the same thread / io_context
		debug_assert (!node.io_ctx.get_executor ().running_in_this_thread ());

		task.cancel ();
		task.join ();
	}
}

auto nano::transport::tcp_server::start_impl () -> asio::awaitable<void>
{
	debug_assert (strand.running_in_this_thread ());
	try
	{
		auto handshake_result = co_await perform_handshake ();
		if (handshake_result != process_result::progress)
		{
			node.logger.debug (nano::log::type::tcp_server, "Handshake aborted: {}", get_remote_endpoint ());
		}
		else
		{
			co_await run_realtime ();
		}
	}
	catch (boost::system::system_error const & ex)
	{
		node.stats.inc (nano::stat::type::tcp_server_error, nano::to_stat_detail (ex.code ()), nano::stat::dir::in);
		node.logger.debug (nano::log::type::tcp_server, "Server stopped due to error: {} ({})", ex.code (), get_remote_endpoint ());
	}
	catch (...)
	{
		release_assert (false, "unexpected exception");
	}
	debug_assert (strand.running_in_this_thread ());

	// Ensure socket gets closed if task is stopped
	close_async ();
}

bool nano::transport::tcp_server::alive () const
{
	return socket->alive ();
}

auto nano::transport::tcp_server::perform_handshake () -> asio::awaitable<process_result>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (get_type () == nano::transport::socket_type::undefined);

	// Initiate handshake if we are the ones initiating the connection
	if (socket->get_endpoint_type () == nano::transport::socket_endpoint::client)
	{
		co_await send_handshake_request ();
	}

	struct handshake_message_visitor : public nano::message_visitor
	{
		bool process{ false };
		std::optional<nano::node_id_handshake> handshake;

		void node_id_handshake (nano::node_id_handshake const & msg) override
		{
			process = true;
			handshake = msg;
		}
	};

	// Two-step handshake
	for (int i = 0; i < 2; ++i)
	{
		auto [message, message_status] = co_await receive_message ();
		if (!message)
		{
			node.logger.debug (nano::log::type::tcp_server, "Error deserializing handshake message: {} ({})",
			to_string (message_status),
			get_remote_endpoint ());
			co_return process_result::abort;
		}

		handshake_message_visitor handshake_visitor{};
		message->visit (handshake_visitor);

		handshake_status status = handshake_status::abort;
		if (handshake_visitor.process)
		{
			release_assert (handshake_visitor.handshake.has_value ());
			status = co_await process_handshake (*handshake_visitor.handshake);
		}
		switch (status)
		{
			case handshake_status::abort:
			case handshake_status::bootstrap: // Legacy bootstrap is no longer supported
			{
				node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort);
				node.logger.debug (nano::log::type::tcp_server, "Aborting handshake: {} ({})",
				to_string (message->type ()),
				get_remote_endpoint ());

				co_return process_result::abort;
			}
			case handshake_status::realtime:
			{
				co_return process_result::progress; // Continue receiving new messages
			}
			case handshake_status::handshake:
			{
				// Continue handshake
			}
		}
	}

	// Failed to complete handshake, abort
	node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_failed);
	node.logger.debug (nano::log::type::tcp_server, "Failed to complete handshake ({})", get_remote_endpoint ());
	co_return process_result::abort;
}

auto nano::transport::tcp_server::run_realtime () -> asio::awaitable<void>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (get_type () == nano::transport::socket_type::realtime);

	node.logger.debug (nano::log::type::tcp_server, "Running realtime connection: {}", get_remote_endpoint ());

	while (!co_await nano::async::cancelled ())
	{
		debug_assert (strand.running_in_this_thread ());

		auto [message, status] = co_await receive_message ();
		if (message)
		{
			realtime_message_visitor realtime_visitor{};
			message->visit (realtime_visitor);

			if (realtime_visitor.process)
			{
				release_assert (channel != nullptr);
				channel->set_last_packet_received (std::chrono::steady_clock::now ());

				// TODO: Throttle if not added
				bool added = node.message_processor.put (std::move (message), channel);
				node.stats.inc (nano::stat::type::tcp_server, added ? nano::stat::detail::message_queued : nano::stat::detail::message_dropped);
			}
			else
			{
				node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::message_ignored);
			}
		}
		else // Error while deserializing message
		{
			debug_assert (status != nano::deserialize_message_status::success);

			node.stats.inc (nano::stat::type::tcp_server_error, to_stat_detail (status));

			switch (status)
			{
				// Avoid too much noise about `duplicate_publish_message` errors
				case nano::deserialize_message_status::duplicate_publish_message:
				{
					node.stats.inc (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message);
				}
				break;
				case nano::deserialize_message_status::duplicate_confirm_ack_message:
				{
					node.stats.inc (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message);
				}
				break;
				default:
				{
					node.logger.debug (nano::log::type::tcp_server, "Error deserializing message: {} ({})",
					to_string (status),
					get_remote_endpoint ());

					co_return; // Stop receiving further messages
				}
				break;
			}
		}
	}
}

auto nano::transport::tcp_server::receive_message () -> asio::awaitable<nano::deserialize_message_result>
{
	debug_assert (strand.running_in_this_thread ());

	node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::read_header, nano::stat::dir::in);
	node.stats.inc (nano::stat::type::tcp_server_read, nano::stat::detail::header, nano::stat::dir::in);

	auto header_payload = co_await read_socket (nano::message_header::size);
	auto header_stream = nano::bufferstream{ header_payload.data (), header_payload.size () };

	bool error = false;
	nano::message_header header{ error, header_stream };

	if (error)
	{
		co_return nano::deserialize_message_result{ nullptr, nano::deserialize_message_status::invalid_header };
	}
	if (!header.is_valid_message_type ())
	{
		co_return nano::deserialize_message_result{ nullptr, nano::deserialize_message_status::invalid_message_type };
	}
	if (header.network != node.config.network_params.network.current_network)
	{
		co_return nano::deserialize_message_result{ nullptr, nano::deserialize_message_status::invalid_network };
	}
	if (header.version_using < node.config.network_params.network.protocol_version_min)
	{
		co_return nano::deserialize_message_result{ nullptr, nano::deserialize_message_status::outdated_version };
	}

	auto const payload_size = header.payload_length_bytes ();

	node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::read_payload, nano::stat::dir::in);
	node.stats.inc (nano::stat::type::tcp_server_read, to_stat_detail (header.type), nano::stat::dir::in);

	auto payload_buffer = payload_size > 0 ? co_await read_socket (payload_size) : nano::buffer_view{ buffer->data (), 0 };

	auto result = nano::deserialize_message (payload_buffer, header,
	node.network_params.network,
	&node.network.filter,
	&node.block_uniquer,
	&node.vote_uniquer);

	auto const & [message, status] = result;
	if (message)
	{
		node.stats.inc (nano::stat::type::tcp_server_message, to_stat_detail (message->type ()), nano::stat::dir::in);
	}

	co_return result;
}

auto nano::transport::tcp_server::read_socket (size_t size) const -> asio::awaitable<nano::buffer_view>
{
	debug_assert (strand.running_in_this_thread ());

	auto [ec, size_read] = co_await socket->co_read (buffer, size);
	debug_assert (ec || size_read == size);
	debug_assert (strand.running_in_this_thread ());

	if (ec)
	{
		throw boost::system::system_error (ec);
	}

	release_assert (size_read == size);
	co_return nano::buffer_view{ buffer->data (), size_read };
}

auto nano::transport::tcp_server::process_handshake (nano::node_id_handshake const & message) -> asio::awaitable<handshake_status>
{
	if (node.flags.disable_tcp_realtime)
	{
		node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_error);
		node.logger.debug (nano::log::type::tcp_server, "Handshake attempted with disabled realtime mode ({})", get_remote_endpoint ());

		co_return handshake_status::abort;
	}
	if (!message.query && !message.response)
	{
		node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_error);
		node.logger.debug (nano::log::type::tcp_server, "Invalid handshake message received ({})", get_remote_endpoint ());

		co_return handshake_status::abort;
	}
	if (message.query && handshake_received) // Second handshake message should be a response only
	{
		node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_error);
		node.logger.debug (nano::log::type::tcp_server, "Detected multiple handshake queries ({})", get_remote_endpoint ());

		co_return handshake_status::abort;
	}

	handshake_received = true;

	node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
	node.logger.debug (nano::log::type::tcp_server, "Handshake message received: {} ({})",
	message.query ? (message.response ? "query + response" : "query") : (message.response ? "response" : "none"),
	get_remote_endpoint ());

	if (message.query)
	{
		// Sends response + our own query
		co_await send_handshake_response (*message.query, message.is_v2 ());
		// Fall through and continue handshake
	}
	if (message.response)
	{
		if (node.network.verify_handshake_response (*message.response, get_remote_endpoint ()))
		{
			bool success = to_realtime_connection (message.response->node_id);
			if (success)
			{
				co_return handshake_status::realtime; // Switched to realtime
			}
			else
			{
				node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_error);
				node.logger.debug (nano::log::type::tcp_server, "Error switching to realtime mode ({})", get_remote_endpoint ());

				co_return handshake_status::abort;
			}
		}
		else
		{
			node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_response_invalid);
			node.logger.debug (nano::log::type::tcp_server, "Invalid handshake response received ({})", get_remote_endpoint ());

			co_return handshake_status::abort;
		}
	}

	co_return handshake_status::handshake; // Handshake is in progress
}

auto nano::transport::tcp_server::send_handshake_request () -> asio::awaitable<void>
{
	auto query = node.network.prepare_handshake_query (get_remote_endpoint ());
	nano::node_id_handshake message{ node.network_params.network, query };

	node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_initiate, nano::stat::dir::out);
	node.logger.debug (nano::log::type::tcp_server, "Initiating handshake query ({})", get_remote_endpoint ());

	auto shared_const_buffer = message.to_shared_const_buffer ();

	auto [ec, size] = co_await socket->co_write (shared_const_buffer, shared_const_buffer.size ());
	debug_assert (ec || size == shared_const_buffer.size ());
	if (ec)
	{
		node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_network_error);
		node.logger.debug (nano::log::type::tcp_server, "Error sending handshake query: {} ({})", ec.message (), get_remote_endpoint ());

		throw boost::system::system_error (ec); // Abort further processing
	}
	else
	{
		node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake, nano::stat::dir::out);
	}
}

auto nano::transport::tcp_server::send_handshake_response (nano::node_id_handshake::query_payload const & query, bool v2) -> asio::awaitable<void>
{
	auto response = node.network.prepare_handshake_response (query, v2);
	auto own_query = node.network.prepare_handshake_query (get_remote_endpoint ());
	nano::node_id_handshake handshake_response{ node.network_params.network, own_query, response };

	node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_response, nano::stat::dir::out);
	node.logger.debug (nano::log::type::tcp_server, "Responding to handshake ({})", get_remote_endpoint ());

	auto shared_const_buffer = handshake_response.to_shared_const_buffer ();

	auto [ec, size] = co_await socket->co_write (shared_const_buffer, shared_const_buffer.size ());
	debug_assert (ec || size == shared_const_buffer.size ());
	if (ec)
	{
		node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_network_error);
		node.logger.debug (nano::log::type::tcp_server, "Error sending handshake response: {} ({})", ec.message (), get_remote_endpoint ());

		throw boost::system::system_error (ec); // Abort further processing
	}
	else
	{
		node.stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_response, nano::stat::dir::out);
	}
}

/*
 * realtime_message_visitor
 */

void nano::transport::tcp_server::realtime_message_visitor::keepalive (const nano::keepalive & message)
{
	process = true;
}

void nano::transport::tcp_server::realtime_message_visitor::publish (const nano::publish & message)
{
	process = true;
}

void nano::transport::tcp_server::realtime_message_visitor::confirm_req (const nano::confirm_req & message)
{
	process = true;
}

void nano::transport::tcp_server::realtime_message_visitor::confirm_ack (const nano::confirm_ack & message)
{
	process = true;
}

void nano::transport::tcp_server::realtime_message_visitor::frontier_req (const nano::frontier_req & message)
{
	process = true;
}

void nano::transport::tcp_server::realtime_message_visitor::telemetry_req (const nano::telemetry_req & message)
{
	process = true;
}

void nano::transport::tcp_server::realtime_message_visitor::telemetry_ack (const nano::telemetry_ack & message)
{
	process = true;
}

void nano::transport::tcp_server::realtime_message_visitor::asc_pull_req (const nano::asc_pull_req & message)
{
	process = true;
}

void nano::transport::tcp_server::realtime_message_visitor::asc_pull_ack (const nano::asc_pull_ack & message)
{
	process = true;
}

/*
 *
 */

bool nano::transport::tcp_server::to_bootstrap_connection ()
{
	if (node.flags.disable_bootstrap_listener)
	{
		return false;
	}
	if (node.tcp_listener.bootstrap_count () >= node.config.bootstrap_connections_max)
	{
		return false;
	}
	if (socket->type () != nano::transport::socket_type::undefined)
	{
		return false;
	}

	socket->type_set (nano::transport::socket_type::bootstrap);

	node.logger.debug (nano::log::type::tcp_server, "Switched to bootstrap mode ({})", get_remote_endpoint ());

	return true;
}

bool nano::transport::tcp_server::to_realtime_connection (nano::account const & node_id)
{
	if (node.flags.disable_tcp_realtime)
	{
		return false;
	}
	if (socket->type () != nano::transport::socket_type::undefined)
	{
		return false;
	}

	auto channel_l = node.network.tcp_channels.create (socket, shared_from_this (), node_id);
	if (!channel_l)
	{
		return false;
	}
	channel = channel_l;

	socket->type_set (nano::transport::socket_type::realtime);

	node.logger.debug (nano::log::type::tcp_server, "Switched to realtime mode ({})", get_remote_endpoint ());

	return true;
}
