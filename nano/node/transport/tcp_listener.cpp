#include <nano/node/messages.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_server.hpp>

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

nano::transport::tcp_listener::~tcp_listener ()
{
	debug_assert (stopped);
}

void nano::transport::tcp_listener::start (std::function<bool (std::shared_ptr<nano::transport::socket> const &, boost::system::error_code const &)> callback_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

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
		stopped = true;
		connections_l.swap (connections);
	}

	nano::lock_guard<nano::mutex> lock{ mutex };
	boost::asio::dispatch (strand, [this_l = shared_from_this ()] () {
		this_l->acceptor.close ();

		for (auto & address_connection_pair : this_l->connections_per_address)
		{
			if (auto connection_l = address_connection_pair.second.lock ())
			{
				connection_l->close ();
			}
		}
		this_l->connections_per_address.clear ();
	});
}

std::size_t nano::transport::tcp_listener::connection_count ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	cleanup ();
	return connections.size ();
}

void nano::transport::tcp_listener::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	erase_if (connections, [] (auto const & connection) {
		return connection.second.expired ();
	});
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
		auto new_connection = std::make_shared<nano::transport::socket> (this_l->node, socket_endpoint::server);
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

boost::asio::ip::tcp::endpoint nano::transport::tcp_listener::endpoint () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (!stopped)
	{
		return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), acceptor.local_endpoint ().port ());
	}
	else
	{
		return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), 0);
	}
}

std::unique_ptr<nano::container_info_component> nano::transport::tcp_listener::collect_container_info (std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "connections", connection_count (), sizeof (decltype (connections)::value_type) }));
	return composite;
}