#include <celerix/node/messages.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/transport/message_deserializer.hpp>
#include <celerix/node/transport/tcp_listener.hpp>
#include <celerix/node/transport/tcp_server.hpp>

#include <memory>

/*
 * tcp_server
 */

celerix::transport::tcp_server::tcp_server (std::shared_ptr<celerix::transport::tcp_socket> socket_a, std::shared_ptr<celerix::node> node_a, bool allow_bootstrap_a) :
	socket{ socket_a },
	node{ node_a },
	allow_bootstrap{ allow_bootstrap_a },
	message_deserializer{
		std::make_shared<celerix::transport::message_deserializer> (node_a->network_params.network, node_a->network.filter, node_a->block_uniquer, node_a->vote_uniquer,
		[socket_l = socket] (std::shared_ptr<std::vector<uint8_t>> const & data_a, size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a) {
			debug_assert (socket_l != nullptr);
			socket_l->read_impl (data_a, size_a, callback_a);
		})
	}
{
	debug_assert (socket != nullptr);
}

celerix::transport::tcp_server::~tcp_server ()
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}

	node->logger.debug (celerix::log::type::tcp_server, "Exiting server: {}", fmt::streamed (remote_endpoint));

	stop ();
}

void celerix::transport::tcp_server::start ()
{
	// Set remote_endpoint
	if (remote_endpoint.port () == 0)
	{
		remote_endpoint = socket->remote_endpoint ();
		debug_assert (remote_endpoint.port () != 0);
	}

	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}

	node->logger.debug (celerix::log::type::tcp_server, "Starting server: {}", fmt::streamed (remote_endpoint));

	receive_message ();
}

void celerix::transport::tcp_server::stop ()
{
	if (!stopped.exchange (true))
	{
		socket->close ();
	}
}

void celerix::transport::tcp_server::receive_message ()
{
	if (stopped)
	{
		return;
	}

	message_deserializer->read ([this_l = shared_from_this ()] (boost::system::error_code ec, std::unique_ptr<celerix::message> message) {
		auto node = this_l->node.lock ();
		if (!node)
		{
			return;
		}
		if (ec)
		{
			// IO error or critical error when deserializing message
			node->stats.inc (celerix::stat::type::error, to_stat_detail (this_l->message_deserializer->status));
			node->logger.debug (celerix::log::type::tcp_server, "Error reading message: {}, status: {} ({})",
			ec.message (),
			to_string (this_l->message_deserializer->status),
			fmt::streamed (this_l->remote_endpoint));

			this_l->stop ();
		}
		else
		{
			this_l->received_message (std::move (message));
		}
	});
}

void celerix::transport::tcp_server::received_message (std::unique_ptr<celerix::message> message)
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}

	process_result result = process_result::progress;
	if (message)
	{
		result = process_message (std::move (message));
	}
	else
	{
		// Error while deserializing message
		debug_assert (message_deserializer->status != transport::parse_status::success);

		node->stats.inc (celerix::stat::type::error, to_stat_detail (message_deserializer->status));

		switch (message_deserializer->status)
		{
			// Avoid too much noise about `duplicate_publish_message` errors
			case celerix::transport::parse_status::duplicate_publish_message:
			{
				node->stats.inc (celerix::stat::type::filter, celerix::stat::detail::duplicate_publish_message);
			}
			break;
			case celerix::transport::parse_status::duplicate_confirm_ack_message:
			{
				node->stats.inc (celerix::stat::type::filter, celerix::stat::detail::duplicate_confirm_ack_message);
			}
			break;
			default:
			{
				node->logger.debug (celerix::log::type::tcp_server, "Error deserializing message: {} ({})",
				to_string (message_deserializer->status),
				fmt::streamed (remote_endpoint));
			}
			break;
		}
	}

	switch (result)
	{
		case process_result::progress:
		{
			receive_message ();
		}
		break;
		case process_result::abort:
		{
			stop ();
		}
		break;
		case process_result::pause:
		{
			// Do nothing
		}
		break;
	}
}

auto celerix::transport::tcp_server::process_message (std::unique_ptr<celerix::message> message) -> process_result
{
	auto node = this->node.lock ();
	if (!node)
	{
		return process_result::abort;
	}

	node->stats.inc (celerix::stat::type::tcp_server, to_stat_detail (message->type ()), celerix::stat::dir::in);

	debug_assert (is_undefined_connection () || is_realtime_connection () || is_bootstrap_connection ());

	/*
	 * Server initially starts in undefined state, where it waits for either a handshake or booststrap request message
	 * If the server receives a handshake (and it is successfully validated) it will switch to a realtime mode.
	 * In realtime mode messages are deserialized and queued to `tcp_message_manager` for further processing.
	 * In realtime mode any bootstrap requests are ignored.
	 *
	 * If the server receives a bootstrap request before receiving a handshake, it will switch to a bootstrap mode.
	 * In bootstrap mode once a valid bootstrap request message is received, the server will start a corresponding bootstrap server and pass control to that server.
	 * Once that server finishes its task, control is passed back to this server to read and process any subsequent messages.
	 * In bootstrap mode any realtime messages are ignored
	 */
	if (is_undefined_connection ())
	{
		handshake_message_visitor handshake_visitor{ *this };
		message->visit (handshake_visitor);

		switch (handshake_visitor.result)
		{
			case handshake_status::abort:
			{
				node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_abort);
				node->logger.debug (celerix::log::type::tcp_server, "Aborting handshake: {} ({})", to_string (message->type ()), fmt::streamed (remote_endpoint));

				return process_result::abort;
			}
			case handshake_status::handshake:
			{
				return process_result::progress; // Continue handshake
			}
			case handshake_status::realtime:
			{
				queue_realtime (std::move (message));
				return process_result::progress; // Continue receiving new messages
			}
			case handshake_status::bootstrap:
			{
				bool success = to_bootstrap_connection ();
				if (!success)
				{
					node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_error);
					node->logger.debug (celerix::log::type::tcp_server, "Error switching to bootstrap mode: {} ({})", to_string (message->type ()), fmt::streamed (remote_endpoint));

					return process_result::abort; // Switch failed, abort
				}
				else
				{
					// Fall through to process the bootstrap message
				}
			}
		}
	}
	else if (is_realtime_connection ())
	{
		realtime_message_visitor realtime_visitor{ *this };
		message->visit (realtime_visitor);

		if (realtime_visitor.process)
		{
			queue_realtime (std::move (message));
		}

		return process_result::progress;
	}
	// The server will switch to bootstrap mode immediately after processing the first bootstrap message, thus no `else if`
	if (is_bootstrap_connection ())
	{
		bootstrap_message_visitor bootstrap_visitor{ shared_from_this () };
		message->visit (bootstrap_visitor);

		// Pause receiving new messages if bootstrap serving started
		return bootstrap_visitor.processed ? process_result::pause : process_result::progress;
	}

	debug_assert (false);
	return process_result::abort;
}

void celerix::transport::tcp_server::queue_realtime (std::unique_ptr<celerix::message> message)
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}

	release_assert (channel != nullptr);

	channel->set_last_packet_received (std::chrono::steady_clock::now ());

	bool added = node->message_processor.put (std::move (message), channel);
	// TODO: Throttle if not added
}

auto celerix::transport::tcp_server::process_handshake (celerix::node_id_handshake const & message) -> handshake_status
{
	auto node = this->node.lock ();
	if (!node)
	{
		return handshake_status::abort;
	}

	if (node->flags.disable_tcp_realtime)
	{
		node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_error);
		node->logger.debug (celerix::log::type::tcp_server, "Handshake attempted with disabled realtime mode ({})", fmt::streamed (remote_endpoint));

		return handshake_status::abort;
	}
	if (!message.query && !message.response)
	{
		node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_error);
		node->logger.debug (celerix::log::type::tcp_server, "Invalid handshake message received ({})", fmt::streamed (remote_endpoint));

		return handshake_status::abort;
	}
	if (message.query && handshake_received) // Second handshake message should be a response only
	{
		node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_error);
		node->logger.debug (celerix::log::type::tcp_server, "Detected multiple handshake queries ({})", fmt::streamed (remote_endpoint));

		return handshake_status::abort;
	}

	handshake_received = true;

	node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::node_id_handshake, celerix::stat::dir::in);
	node->logger.debug (celerix::log::type::tcp_server, "Handshake message received: {} ({})",
	message.query ? (message.response ? "query + response" : "query") : (message.response ? "response" : "none"),
	fmt::streamed (remote_endpoint));

	if (message.query)
	{
		// Sends response + our own query
		send_handshake_response (*message.query, message.is_v2 ());
		// Fall through and continue handshake
	}
	if (message.response)
	{
		if (node->network.verify_handshake_response (*message.response, celerix::transport::map_tcp_to_endpoint (remote_endpoint)))
		{
			bool success = to_realtime_connection (message.response->node_id);
			if (success)
			{
				return handshake_status::realtime; // Switched to realtime
			}
			else
			{
				node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_error);
				node->logger.debug (celerix::log::type::tcp_server, "Error switching to realtime mode ({})", fmt::streamed (remote_endpoint));

				return handshake_status::abort;
			}
		}
		else
		{
			node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_response_invalid);
			node->logger.debug (celerix::log::type::tcp_server, "Invalid handshake response received ({})", fmt::streamed (remote_endpoint));

			return handshake_status::abort;
		}
	}

	return handshake_status::handshake; // Handshake is in progress
}

void celerix::transport::tcp_server::initiate_handshake ()
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}

	auto query = node->network.prepare_handshake_query (celerix::transport::map_tcp_to_endpoint (remote_endpoint));
	celerix::node_id_handshake message{ node->network_params.network, query };

	node->logger.debug (celerix::log::type::tcp_server, "Initiating handshake query ({})", fmt::streamed (remote_endpoint));

	auto shared_const_buffer = message.to_shared_const_buffer ();
	socket->async_write (shared_const_buffer, [this_l = shared_from_this ()] (boost::system::error_code const & ec, std::size_t size_a) {
		auto node = this_l->node.lock ();
		if (!node)
		{
			return;
		}
		if (ec)
		{
			node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_network_error);
			node->logger.debug (celerix::log::type::tcp_server, "Error sending handshake query: {} ({})", ec.message (), fmt::streamed (this_l->remote_endpoint));

			// Stop invalid handshake
			this_l->stop ();
		}
		else
		{
			node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake, celerix::stat::dir::out);
			node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_initiate, celerix::stat::dir::out);
		}
	});
}

void celerix::transport::tcp_server::send_handshake_response (celerix::node_id_handshake::query_payload const & query, bool v2)
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}

	auto response = node->network.prepare_handshake_response (query, v2);
	auto own_query = node->network.prepare_handshake_query (celerix::transport::map_tcp_to_endpoint (remote_endpoint));
	celerix::node_id_handshake handshake_response{ node->network_params.network, own_query, response };

	node->logger.debug (celerix::log::type::tcp_server, "Responding to handshake ({})", fmt::streamed (remote_endpoint));

	auto shared_const_buffer = handshake_response.to_shared_const_buffer ();
	socket->async_write (shared_const_buffer, [this_l = shared_from_this ()] (boost::system::error_code const & ec, std::size_t size_a) {
		auto node = this_l->node.lock ();
		if (!node)
		{
			return;
		}
		if (ec)
		{
			node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_network_error);
			node->logger.debug (celerix::log::type::tcp_server, "Error sending handshake response: {} ({})", ec.message (), fmt::streamed (this_l->remote_endpoint));

			// Stop invalid handshake
			this_l->stop ();
		}
		else
		{
			node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake, celerix::stat::dir::out);
			node->stats.inc (celerix::stat::type::tcp_server, celerix::stat::detail::handshake_response, celerix::stat::dir::out);
		}
	});
}

/*
 * handshake_message_visitor
 */

void celerix::transport::tcp_server::handshake_message_visitor::node_id_handshake (const celerix::node_id_handshake & message)
{
	result = server.process_handshake (message);
}

void celerix::transport::tcp_server::handshake_message_visitor::bulk_pull (const celerix::bulk_pull & message)
{
	result = handshake_status::bootstrap;
}

void celerix::transport::tcp_server::handshake_message_visitor::bulk_pull_account (const celerix::bulk_pull_account & message)
{
	result = handshake_status::bootstrap;
}

void celerix::transport::tcp_server::handshake_message_visitor::bulk_push (const celerix::bulk_push & message)
{
	result = handshake_status::bootstrap;
}

void celerix::transport::tcp_server::handshake_message_visitor::frontier_req (const celerix::frontier_req & message)
{
	result = handshake_status::bootstrap;
}

/*
 * realtime_message_visitor
 */

void celerix::transport::tcp_server::realtime_message_visitor::keepalive (const celerix::keepalive & message)
{
	process = true;
	server.set_last_keepalive (message);
}

void celerix::transport::tcp_server::realtime_message_visitor::publish (const celerix::publish & message)
{
	process = true;
}

void celerix::transport::tcp_server::realtime_message_visitor::confirm_req (const celerix::confirm_req & message)
{
	process = true;
}

void celerix::transport::tcp_server::realtime_message_visitor::confirm_ack (const celerix::confirm_ack & message)
{
	process = true;
}

void celerix::transport::tcp_server::realtime_message_visitor::frontier_req (const celerix::frontier_req & message)
{
	process = true;
}

void celerix::transport::tcp_server::realtime_message_visitor::telemetry_req (const celerix::telemetry_req & message)
{
	auto node = server.node.lock ();
	if (!node)
	{
		return;
	}
	// Only handle telemetry requests if they are outside the cooldown period
	if (server.last_telemetry_req + node->network_params.network.telemetry_request_cooldown < std::chrono::steady_clock::now ())
	{
		server.last_telemetry_req = std::chrono::steady_clock::now ();
		process = true;
	}
	else
	{
		node->stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::request_within_protection_cache_zone);
	}
}

void celerix::transport::tcp_server::realtime_message_visitor::telemetry_ack (const celerix::telemetry_ack & message)
{
	process = true;
}

void celerix::transport::tcp_server::realtime_message_visitor::asc_pull_req (const celerix::asc_pull_req & message)
{
	process = true;
}

void celerix::transport::tcp_server::realtime_message_visitor::asc_pull_ack (const celerix::asc_pull_ack & message)
{
	process = true;
}

/*
 * bootstrap_message_visitor
 */

celerix::transport::tcp_server::bootstrap_message_visitor::bootstrap_message_visitor (std::shared_ptr<tcp_server> server) :
	server{ std::move (server) }
{
}

void celerix::transport::tcp_server::bootstrap_message_visitor::bulk_pull (const celerix::bulk_pull & message)
{
	// Ignored since V28
	// TODO: Abort connection?
}

void celerix::transport::tcp_server::bootstrap_message_visitor::bulk_pull_account (const celerix::bulk_pull_account & message)
{
	// Ignored since V28
	// TODO: Abort connection?
}

void celerix::transport::tcp_server::bootstrap_message_visitor::bulk_push (const celerix::bulk_push &)
{
	// Ignored since V28
	// TODO: Abort connection?
}

void celerix::transport::tcp_server::bootstrap_message_visitor::frontier_req (const celerix::frontier_req & message)
{
	// Ignored since V28
	// TODO: Abort connection?
}

/*
 *
 */

// TODO: We could periodically call this (from a dedicated timeout thread for eg.) but socket already handles timeouts,
//  and since we only ever store tcp_server as weak_ptr, socket timeout will automatically trigger tcp_server cleanup
void celerix::transport::tcp_server::timeout ()
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}
	if (socket->has_timed_out ())
	{
		node->logger.debug (celerix::log::type::tcp_server, "Closing TCP server due to timeout ({})", fmt::streamed (remote_endpoint));

		socket->close ();
	}
}

void celerix::transport::tcp_server::set_last_keepalive (celerix::keepalive const & message)
{
	std::lock_guard<celerix::mutex> lock{ mutex };
	if (!last_keepalive)
	{
		last_keepalive = message;
	}
}

std::optional<celerix::keepalive> celerix::transport::tcp_server::pop_last_keepalive ()
{
	std::lock_guard<celerix::mutex> lock{ mutex };
	auto result = last_keepalive;
	last_keepalive = std::nullopt;
	return result;
}

bool celerix::transport::tcp_server::to_bootstrap_connection ()
{
	auto node = this->node.lock ();
	if (!node)
	{
		return false;
	}
	if (!allow_bootstrap)
	{
		return false;
	}
	if (node->flags.disable_bootstrap_listener)
	{
		return false;
	}
	if (node->tcp_listener.bootstrap_count () >= node->config.bootstrap_connections_max)
	{
		return false;
	}
	if (socket->type () != celerix::transport::socket_type::undefined)
	{
		return false;
	}

	socket->type_set (celerix::transport::socket_type::bootstrap);

	node->logger.debug (celerix::log::type::tcp_server, "Switched to bootstrap mode ({})", fmt::streamed (remote_endpoint));

	return true;
}

bool celerix::transport::tcp_server::to_realtime_connection (celerix::account const & node_id)
{
	auto node = this->node.lock ();
	if (!node)
	{
		return false;
	}
	if (node->flags.disable_tcp_realtime)
	{
		return false;
	}
	if (socket->type () != celerix::transport::socket_type::undefined)
	{
		return false;
	}

	auto channel_l = node->network.tcp_channels.create (socket, shared_from_this (), node_id);
	if (!channel_l)
	{
		return false;
	}
	channel = channel_l;

	socket->type_set (celerix::transport::socket_type::realtime);

	node->logger.debug (celerix::log::type::tcp_server, "Switched to realtime mode ({})", fmt::streamed (remote_endpoint));

	return true;
}

bool celerix::transport::tcp_server::is_undefined_connection () const
{
	return socket->type () == celerix::transport::socket_type::undefined;
}

bool celerix::transport::tcp_server::is_bootstrap_connection () const
{
	return socket->is_bootstrap_connection ();
}

bool celerix::transport::tcp_server::is_realtime_connection () const
{
	return socket->is_realtime_connection ();
}
