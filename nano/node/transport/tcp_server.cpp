#include <nano/node/bootstrap/bootstrap_bulk_push.hpp>
#include <nano/node/bootstrap/bootstrap_frontier.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/message_deserializer.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/node/transport/tcp_server.hpp>

#include <boost/format.hpp>

#include <memory>

namespace
{
bool is_temporary_error (boost::system::error_code const & ec_a)
{
	switch (ec_a.value ())
	{
#if EAGAIN != EWOULDBLOCK
		case EAGAIN:
#endif

		case EWOULDBLOCK:
		case EINTR:
			return true;
		default:
			return false;
	}
}
}

/*
 * tcp_listener
 */

nano::transport::tcp_listener::tcp_listener (uint16_t port_a, nano::node & node_a, std::size_t max_inbound_connections) :
	node (node_a),
	strand{ node_a.io_ctx.get_executor () },
	acceptor{ node_a.io_ctx },
	local{ boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::any (), port_a } },
	max_inbound_connections{ max_inbound_connections }
{
}

void nano::transport::tcp_listener::start (std::function<bool (std::shared_ptr<nano::transport::socket> const &, boost::system::error_code const &)> callback_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	on = true;
	acceptor.open (local.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
	boost::system::error_code ec;
	acceptor.bind (local, ec);
	if (!ec)
	{
		acceptor.listen (boost::asio::socket_base::max_listen_connections, ec);
	}
	if (ec)
	{
		node.logger.critical (nano::log::type::tcp_listener, "Error while binding for incoming TCP: {} (port: {})", ec.message (), acceptor.local_endpoint ().port ());
		throw std::runtime_error (ec.message ());
	}

	on_connection (callback_a);
}

void nano::transport::tcp_listener::stop ()
{
	decltype (connections) connections_l;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		on = false;
		connections_l.swap (connections);
	}
	nano::lock_guard<nano::mutex> lock{ mutex };
	boost::asio::dispatch (strand, boost::asio::bind_executor (strand, [this_l = shared_from_this ()] () {
		this_l->acceptor.close ();
		for (auto & address_connection_pair : this_l->connections_per_address)
		{
			if (auto connection_l = address_connection_pair.second.lock ())
			{
				connection_l->close ();
			}
		}
		this_l->connections_per_address.clear ();
	}));
}

std::size_t nano::transport::tcp_listener::connection_count ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return connections.size ();
}

bool nano::transport::tcp_listener::limit_reached_for_incoming_subnetwork_connections (std::shared_ptr<nano::transport::socket> const & new_connection)
{
	debug_assert (strand.running_in_this_thread ());
	if (node.flags.disable_max_peers_per_subnetwork || nano::transport::is_ipv4_or_v4_mapped_address (new_connection->remote.address ()))
	{
		// If the limit is disabled, then it is unreachable.
		// If the address is IPv4 we don't check for a network limit, since its address space isn't big as IPv6 /64.
		return false;
	}
	auto const counted_connections = socket_functions::count_subnetwork_connections (
	connections_per_address,
	new_connection->remote.address ().to_v6 (),
	node.network_params.network.ipv6_subnetwork_prefix_for_limiting);
	return counted_connections >= node.network_params.network.max_peers_per_subnetwork;
}

bool nano::transport::tcp_listener::limit_reached_for_incoming_ip_connections (std::shared_ptr<nano::transport::socket> const & new_connection)
{
	debug_assert (strand.running_in_this_thread ());
	if (node.flags.disable_max_peers_per_ip)
	{
		// If the limit is disabled, then it is unreachable.
		return false;
	}
	auto const address_connections_range = connections_per_address.equal_range (new_connection->remote.address ());
	auto const counted_connections = static_cast<std::size_t> (std::abs (std::distance (address_connections_range.first, address_connections_range.second)));
	return counted_connections >= node.network_params.network.max_peers_per_ip;
}

void nano::transport::tcp_listener::on_connection (std::function<bool (std::shared_ptr<nano::transport::socket> const &, boost::system::error_code const &)> callback_a)
{
	boost::asio::post (strand, boost::asio::bind_executor (strand, [this_l = shared_from_this (), callback = std::move (callback_a)] () mutable {
		if (!this_l->acceptor.is_open ())
		{
			this_l->node.logger.error (nano::log::type::tcp_listener, "Acceptor is not open");
			return;
		}

		// Prepare new connection
		auto new_connection = std::make_shared<nano::transport::socket> (this_l->node, socket::endpoint_type_t::server);
		this_l->acceptor.async_accept (new_connection->tcp_socket, new_connection->remote,
		boost::asio::bind_executor (this_l->strand,
		[this_l, new_connection, cbk = std::move (callback)] (boost::system::error_code const & ec_a) mutable {
			this_l->evict_dead_connections ();

			if (this_l->connections_per_address.size () >= this_l->max_inbound_connections)
			{
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_accept_failure, nano::stat::dir::in);
				this_l->node.logger.debug (nano::log::type::tcp_listener, "Max connections reached ({}), unable to open new connection", this_l->connections_per_address.size ());

				this_l->on_connection_requeue_delayed (std::move (cbk));
				return;
			}

			if (this_l->limit_reached_for_incoming_ip_connections (new_connection))
			{
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_ip, nano::stat::dir::in);
				this_l->node.logger.debug (nano::log::type::tcp_listener, "Max connections per IP reached ({}), unable to open new connection", new_connection->remote_endpoint ().address ().to_string ());

				this_l->on_connection_requeue_delayed (std::move (cbk));
				return;
			}

			if (this_l->limit_reached_for_incoming_subnetwork_connections (new_connection))
			{
				auto const remote_ip_address = new_connection->remote_endpoint ().address ();
				debug_assert (remote_ip_address.is_v6 ());
				auto const remote_subnet = socket_functions::get_ipv6_subnet_address (remote_ip_address.to_v6 (), this_l->node.network_params.network.max_peers_per_subnetwork);

				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_subnetwork, nano::stat::dir::in);
				this_l->node.logger.debug (nano::log::type::tcp_listener, "Max connections per subnetwork reached (subnetwork: {}, ip: {}), unable to open new connection",
				remote_subnet.canonical ().to_string (),
				remote_ip_address.to_string ());

				this_l->on_connection_requeue_delayed (std::move (cbk));
				return;
			}

			if (!ec_a)
			{
				{
					// Best effort attempt to get endpoint addresses
					boost::system::error_code ec;
					new_connection->local = new_connection->tcp_socket.local_endpoint (ec);
				}

				// Make sure the new connection doesn't idle. Note that in most cases, the callback is going to start
				// an IO operation immediately, which will start a timer.
				new_connection->start ();
				new_connection->set_timeout (this_l->node.network_params.network.idle_timeout);
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_accept_success, nano::stat::dir::in);
				this_l->connections_per_address.emplace (new_connection->remote.address (), new_connection);
				this_l->node.observers.socket_accepted.notify (*new_connection);
				if (cbk (new_connection, ec_a))
				{
					this_l->on_connection (std::move (cbk));
					return;
				}
				this_l->node.logger.warn (nano::log::type::tcp_listener, "Stopping to accept new connections");
				return;
			}

			// accept error
			this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_accept_failure, nano::stat::dir::in);
			this_l->node.logger.error (nano::log::type::tcp_listener, "Unable to accept connection: {} ({})", ec_a.message (), new_connection->remote_endpoint ().address ().to_string ());

			if (is_temporary_error (ec_a))
			{
				// if it is a temporary error, just retry it
				this_l->on_connection_requeue_delayed (std::move (cbk));
				return;
			}

			// if it is not a temporary error, check how the listener wants to handle this error
			if (cbk (new_connection, ec_a))
			{
				this_l->on_connection_requeue_delayed (std::move (cbk));
				return;
			}

			// No requeue if we reach here, no incoming socket connections will be handled
			this_l->node.logger.warn (nano::log::type::tcp_listener, "Stopping to accept new connections");
		}));
	}));
}

// If we are unable to accept a socket, for any reason, we wait just a little (1ms) before rescheduling the next connection accept.
// The intention is to throttle back the connection requests and break up any busy loops that could possibly form and
// give the rest of the system a chance to recover.
void nano::transport::tcp_listener::on_connection_requeue_delayed (std::function<bool (std::shared_ptr<nano::transport::socket> const &, boost::system::error_code const &)> callback_a)
{
	node.workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::milliseconds (1), [this_l = shared_from_this (), callback = std::move (callback_a)] () mutable {
		this_l->on_connection (std::move (callback));
	});
}

// This must be called from a strand
void nano::transport::tcp_listener::evict_dead_connections ()
{
	debug_assert (strand.running_in_this_thread ());

	erase_if (connections_per_address, [] (auto const & entry) {
		return entry.second.expired ();
	});
}

void nano::transport::tcp_listener::accept_action (boost::system::error_code const & ec, std::shared_ptr<nano::transport::socket> const & socket_a)
{
	if (!node.network.excluded_peers.check (socket_a->remote_endpoint ()))
	{
		auto server = std::make_shared<nano::transport::tcp_server> (socket_a, node.shared (), true);
		nano::lock_guard<nano::mutex> lock{ mutex };
		connections[server.get ()] = server;
		server->start ();
	}
	else
	{
		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_excluded);
		node.logger.debug (nano::log::type::tcp_server, "Rejected connection from excluded peer: {}", nano::util::to_str (socket_a->remote_endpoint ()));
	}
}

boost::asio::ip::tcp::endpoint nano::transport::tcp_listener::endpoint ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (on)
	{
		return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), acceptor.local_endpoint ().port ());
	}
	else
	{
		return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), 0);
	}
}

std::unique_ptr<nano::container_info_component> nano::transport::collect_container_info (nano::transport::tcp_listener & bootstrap_listener, std::string const & name)
{
	auto sizeof_element = sizeof (decltype (bootstrap_listener.connections)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "connections", bootstrap_listener.connection_count (), sizeof_element }));
	return composite;
}

/*
 * tcp_server
 */

nano::transport::tcp_server::tcp_server (std::shared_ptr<nano::transport::socket> socket_a, std::shared_ptr<nano::node> node_a, bool allow_bootstrap_a) :
	socket{ socket_a },
	node{ node_a },
	allow_bootstrap{ allow_bootstrap_a },
	message_deserializer{
		std::make_shared<nano::transport::message_deserializer> (node_a->network_params.network, node_a->network.publish_filter, node_a->block_uniquer, node_a->vote_uniquer,
		[socket_l = socket] (std::shared_ptr<std::vector<uint8_t>> const & data_a, size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a) {
			debug_assert (socket_l != nullptr);
			socket_l->read_impl (data_a, size_a, callback_a);
		})
	}
{
	debug_assert (socket != nullptr);
}

nano::transport::tcp_server::~tcp_server ()
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}

	node->logger.debug (nano::log::type::tcp_server, "Exiting TCP server ({})", fmt::streamed (remote_endpoint));

	if (socket->type () == nano::transport::socket::type_t::bootstrap)
	{
		--node->tcp_listener->bootstrap_count;
	}
	else if (socket->type () == nano::transport::socket::type_t::realtime)
	{
		--node->tcp_listener->realtime_count;

		// Clear temporary channel
		auto exisiting_response_channel (node->network.tcp_channels.find_channel (remote_endpoint));
		if (exisiting_response_channel != nullptr)
		{
			exisiting_response_channel->temporary = false;
			node->network.tcp_channels.erase (remote_endpoint);
		}
	}

	stop ();

	nano::lock_guard<nano::mutex> lock{ node->tcp_listener->mutex };
	node->tcp_listener->connections.erase (this);
}

void nano::transport::tcp_server::start ()
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

	node->logger.debug (nano::log::type::tcp_server, "Starting TCP server ({})", fmt::streamed (remote_endpoint));

	receive_message ();
}

void nano::transport::tcp_server::stop ()
{
	if (!stopped.exchange (true))
	{
		socket->close ();
	}
}

void nano::transport::tcp_server::receive_message ()
{
	if (stopped)
	{
		return;
	}

	message_deserializer->read ([this_l = shared_from_this ()] (boost::system::error_code ec, std::unique_ptr<nano::message> message) {
		auto node = this_l->node.lock ();
		if (!node)
		{
			return;
		}
		if (ec)
		{
			// IO error or critical error when deserializing message
			node->stats.inc (nano::stat::type::error, to_stat_detail (this_l->message_deserializer->status));
			node->logger.debug (nano::log::type::tcp_server, "Error reading message: {}, status: {} ({})",
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

void nano::transport::tcp_server::received_message (std::unique_ptr<nano::message> message)
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

		node->stats.inc (nano::stat::type::error, to_stat_detail (message_deserializer->status));

		// Avoid too much noise about `duplicate_publish_message` errors
		if (message_deserializer->status == transport::parse_status::duplicate_publish_message)
		{
			node->stats.inc (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message);
		}
		else
		{
			node->logger.debug (nano::log::type::tcp_server, "Error deserializing message: {} ({})",
			to_string (message_deserializer->status),
			fmt::streamed (remote_endpoint));
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

auto nano::transport::tcp_server::process_message (std::unique_ptr<nano::message> message) -> process_result
{
	auto node = this->node.lock ();
	if (!node)
	{
		return process_result::abort;
	}

	node->stats.inc (nano::stat::type::tcp_server, to_stat_detail (message->type ()), nano::stat::dir::in);

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
				node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort);
				node->logger.debug (nano::log::type::tcp_server, "Aborting handshake: {} ({})", to_string (message->type ()), fmt::streamed (remote_endpoint));

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
					node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_error);
					node->logger.debug (nano::log::type::tcp_server, "Error switching to bootstrap mode: {} ({})", to_string (message->type ()), fmt::streamed (remote_endpoint));

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

void nano::transport::tcp_server::queue_realtime (std::unique_ptr<nano::message> message)
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}
	node->network.tcp_channels.message_manager.put_message (nano::tcp_message_item{ std::move (message), remote_endpoint, remote_node_id, socket });
}

auto nano::transport::tcp_server::process_handshake (nano::node_id_handshake const & message) -> handshake_status
{
	auto node = this->node.lock ();
	if (!node)
	{
		return handshake_status::abort;
	}

	if (node->flags.disable_tcp_realtime)
	{
		node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_error);
		node->logger.debug (nano::log::type::tcp_server, "Handshake attempted with disabled realtime TCP ({})", fmt::streamed (remote_endpoint));

		return handshake_status::abort;
	}
	if (!message.query && !message.response)
	{
		node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_error);
		node->logger.debug (nano::log::type::tcp_server, "Invalid handshake message received ({})", fmt::streamed (remote_endpoint));

		return handshake_status::abort;
	}
	if (message.query && handshake_received) // Second handshake message should be a response only
	{
		node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_error);
		node->logger.debug (nano::log::type::tcp_server, "Detected multiple handshake queries ({})", fmt::streamed (remote_endpoint));

		return handshake_status::abort;
	}

	handshake_received = true;

	node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
	node->logger.debug (nano::log::type::tcp_server, "Handshake message received ({})", fmt::streamed (remote_endpoint));

	if (message.query)
	{
		// Sends response + our own query
		send_handshake_response (*message.query, message.is_v2 ());
		// Fall through and continue handshake
	}
	if (message.response)
	{
		if (node->network.verify_handshake_response (*message.response, nano::transport::map_tcp_to_endpoint (remote_endpoint)))
		{
			bool success = to_realtime_connection (message.response->node_id);
			if (success)
			{
				return handshake_status::realtime; // Switched to realtime
			}
			else
			{
				node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_error);
				node->logger.debug (nano::log::type::tcp_server, "Error switching to realtime mode ({})", fmt::streamed (remote_endpoint));

				return handshake_status::abort;
			}
		}
		else
		{
			node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_response_invalid);
			node->logger.debug (nano::log::type::tcp_server, "Invalid handshake response received ({})", fmt::streamed (remote_endpoint));

			return handshake_status::abort;
		}
	}

	return handshake_status::handshake; // Handshake is in progress
}

void nano::transport::tcp_server::send_handshake_response (nano::node_id_handshake::query_payload const & query, bool v2)
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}

	auto response = node->network.prepare_handshake_response (query, v2);
	auto own_query = node->network.prepare_handshake_query (nano::transport::map_tcp_to_endpoint (remote_endpoint));
	nano::node_id_handshake handshake_response{ node->network_params.network, own_query, response };

	node->logger.debug (nano::log::type::tcp_server, "Responding to handshake ({})", fmt::streamed (remote_endpoint));

	auto shared_const_buffer = handshake_response.to_shared_const_buffer ();
	socket->async_write (shared_const_buffer, [this_l = shared_from_this ()] (boost::system::error_code const & ec, std::size_t size_a) {
		auto node = this_l->node.lock ();
		if (!node)
		{
			return;
		}
		if (ec)
		{
			node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_network_error);
			node->logger.debug (nano::log::type::tcp_server, "Error sending handshake response: {} ({})", ec.message (), fmt::streamed (this_l->remote_endpoint));

			// Stop invalid handshake
			this_l->stop ();
		}
		else
		{
			node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake, nano::stat::dir::out);
			node->stats.inc (nano::stat::type::tcp_server, nano::stat::detail::handshake_response, nano::stat::dir::out);
		}
	});
}

/*
 * handshake_message_visitor
 */

void nano::transport::tcp_server::handshake_message_visitor::node_id_handshake (const nano::node_id_handshake & message)
{
	result = server.process_handshake (message);
}

void nano::transport::tcp_server::handshake_message_visitor::bulk_pull (const nano::bulk_pull & message)
{
	result = handshake_status::bootstrap;
}

void nano::transport::tcp_server::handshake_message_visitor::bulk_pull_account (const nano::bulk_pull_account & message)
{
	result = handshake_status::bootstrap;
}

void nano::transport::tcp_server::handshake_message_visitor::bulk_push (const nano::bulk_push & message)
{
	result = handshake_status::bootstrap;
}

void nano::transport::tcp_server::handshake_message_visitor::frontier_req (const nano::frontier_req & message)
{
	result = handshake_status::bootstrap;
}

/*
 * realtime_message_visitor
 */

void nano::transport::tcp_server::realtime_message_visitor::keepalive (const nano::keepalive & message)
{
	process = true;
	server.set_last_keepalive (message);
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
		node->stats.inc (nano::stat::type::telemetry, nano::stat::detail::request_within_protection_cache_zone);
	}
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
 * bootstrap_message_visitor
 */

nano::transport::tcp_server::bootstrap_message_visitor::bootstrap_message_visitor (std::shared_ptr<tcp_server> server) :
	server{ std::move (server) }
{
}

void nano::transport::tcp_server::bootstrap_message_visitor::bulk_pull (const nano::bulk_pull & message)
{
	auto node = server->node.lock ();
	if (!node)
	{
		return;
	}
	if (node->flags.disable_bootstrap_bulk_pull_server)
	{
		return;
	}

	node->bootstrap_workers.push_task ([server = server, message = message] () {
		// TODO: Add completion callback to bulk pull server
		// TODO: There should be no need to re-copy message as unique pointer, refactor those bulk/frontier pull/push servers
		auto bulk_pull_server = std::make_shared<nano::bulk_pull_server> (server, std::make_unique<nano::bulk_pull> (message));
		bulk_pull_server->send_next ();
	});

	processed = true;
}

void nano::transport::tcp_server::bootstrap_message_visitor::bulk_pull_account (const nano::bulk_pull_account & message)
{
	auto node = server->node.lock ();
	if (!node)
	{
		return;
	}
	if (node->flags.disable_bootstrap_bulk_pull_server)
	{
		return;
	}

	node->bootstrap_workers.push_task ([server = server, message = message] () {
		// TODO: Add completion callback to bulk pull server
		// TODO: There should be no need to re-copy message as unique pointer, refactor those bulk/frontier pull/push servers
		auto bulk_pull_account_server = std::make_shared<nano::bulk_pull_account_server> (server, std::make_unique<nano::bulk_pull_account> (message));
		bulk_pull_account_server->send_frontier ();
	});

	processed = true;
}

void nano::transport::tcp_server::bootstrap_message_visitor::bulk_push (const nano::bulk_push &)
{
	auto node = server->node.lock ();
	if (!node)
	{
		return;
	}
	node->bootstrap_workers.push_task ([server = server] () {
		// TODO: Add completion callback to bulk pull server
		auto bulk_push_server = std::make_shared<nano::bulk_push_server> (server);
		bulk_push_server->throttled_receive ();
	});

	processed = true;
}

void nano::transport::tcp_server::bootstrap_message_visitor::frontier_req (const nano::frontier_req & message)
{
	auto node = server->node.lock ();
	if (!node)
	{
		return;
	}

	node->bootstrap_workers.push_task ([server = server, message = message] () {
		// TODO: There should be no need to re-copy message as unique pointer, refactor those bulk/frontier pull/push servers
		auto response = std::make_shared<nano::frontier_req_server> (server, std::make_unique<nano::frontier_req> (message));
		response->send_next ();
	});

	processed = true;
}

/*
 *
 */

// TODO: We could periodically call this (from a dedicated timeout thread for eg.) but socket already handles timeouts,
//  and since we only ever store tcp_server as weak_ptr, socket timeout will automatically trigger tcp_server cleanup
void nano::transport::tcp_server::timeout ()
{
	auto node = this->node.lock ();
	if (!node)
	{
		return;
	}
	if (socket->has_timed_out ())
	{
		node->logger.debug (nano::log::type::tcp_server, "Closing TCP server due to timeout ({})", fmt::streamed (remote_endpoint));

		{
			nano::lock_guard<nano::mutex> lock{ node->tcp_listener->mutex };
			node->tcp_listener->connections.erase (this);
		}
		socket->close ();
	}
}

void nano::transport::tcp_server::set_last_keepalive (nano::keepalive const & message)
{
	std::lock_guard<nano::mutex> lock{ mutex };
	if (!last_keepalive)
	{
		last_keepalive = message;
	}
}

std::optional<nano::keepalive> nano::transport::tcp_server::pop_last_keepalive ()
{
	std::lock_guard<nano::mutex> lock{ mutex };
	auto result = last_keepalive;
	last_keepalive = std::nullopt;
	return result;
}

bool nano::transport::tcp_server::to_bootstrap_connection ()
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
	if (node->tcp_listener->bootstrap_count >= node->config.bootstrap_connections_max)
	{
		return false;
	}
	if (socket->type () != nano::transport::socket::type_t::undefined)
	{
		return false;
	}

	++node->tcp_listener->bootstrap_count;
	socket->type_set (nano::transport::socket::type_t::bootstrap);

	node->logger.debug (nano::log::type::tcp_server, "Switched to bootstrap mode ({})", fmt::streamed (remote_endpoint));

	return true;
}

bool nano::transport::tcp_server::to_realtime_connection (nano::account const & node_id)
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
	if (socket->type () != nano::transport::socket::type_t::undefined)
	{
		return false;
	}

	remote_node_id = node_id;
	++node->tcp_listener->realtime_count;
	socket->type_set (nano::transport::socket::type_t::realtime);

	node->logger.debug (nano::log::type::tcp_server, "Switched to realtime mode ({})", fmt::streamed (remote_endpoint));

	return true;
}

bool nano::transport::tcp_server::is_undefined_connection () const
{
	return socket->type () == nano::transport::socket::type_t::undefined;
}

bool nano::transport::tcp_server::is_bootstrap_connection () const
{
	return socket->is_bootstrap_connection ();
}

bool nano::transport::tcp_server::is_realtime_connection () const
{
	return socket->is_realtime_connection ();
}
