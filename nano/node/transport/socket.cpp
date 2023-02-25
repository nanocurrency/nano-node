#include <nano/boost/asio/bind_executor.hpp>
#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/boost/asio/read.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/socket.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/format.hpp>

#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

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

nano::transport::socket::socket (nano::node & node_a, endpoint_type_t endpoint_type_a) :
	strand{ node_a.io_ctx.get_executor () },
	tcp_socket{ node_a.io_ctx },
	node{ node_a },
	endpoint_type_m{ endpoint_type_a },
	timeout{ std::numeric_limits<uint64_t>::max () },
	last_completion_time_or_init{ nano::seconds_since_epoch () },
	last_receive_time_or_init{ nano::seconds_since_epoch () },
	default_timeout{ node_a.config.tcp_io_timeout },
	silent_connection_tolerance_time{ node_a.network_params.network.silent_connection_tolerance_time }
{
}

nano::transport::socket::~socket ()
{
	close_internal ();
}

void nano::transport::socket::async_connect (nano::tcp_endpoint const & endpoint_a, std::function<void (boost::system::error_code const &)> callback_a)
{
	debug_assert (callback_a);
	debug_assert (endpoint_type () == endpoint_type_t::client);

	checkup ();
	auto this_l (shared_from_this ());
	set_default_timeout ();

	this_l->tcp_socket.async_connect (endpoint_a,
	boost::asio::bind_executor (this_l->strand,
	[this_l, callback = std::move (callback_a), endpoint_a] (boost::system::error_code const & ec) {
		if (ec)
		{
			this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_connect_error, nano::stat::dir::in);
			this_l->close ();
		}
		else
		{
			this_l->set_last_completion ();
		}
		this_l->remote = endpoint_a;
		this_l->node.observers.socket_connected.notify (*this_l);
		callback (ec);
	}));
}

void nano::transport::socket::async_read (std::shared_ptr<std::vector<uint8_t>> const & buffer_a, std::size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a)
{
	debug_assert (callback_a);

	if (size_a <= buffer_a->size ())
	{
		auto this_l (shared_from_this ());
		if (!closed)
		{
			set_default_timeout ();
			boost::asio::post (strand, boost::asio::bind_executor (strand, [buffer_a, callback = std::move (callback_a), size_a, this_l] () mutable {
				boost::asio::async_read (this_l->tcp_socket, boost::asio::buffer (buffer_a->data (), size_a),
				boost::asio::bind_executor (this_l->strand,
				[this_l, buffer_a, cbk = std::move (callback)] (boost::system::error_code const & ec, std::size_t size_a) {
					if (ec)
					{
						this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_read_error, nano::stat::dir::in);
						this_l->close ();
					}
					else
					{
						this_l->node.stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::in, size_a);
						this_l->set_last_completion ();
						this_l->set_last_receive_time ();
					}
					cbk (ec, size_a);
				}));
			}));
		}
	}
	else
	{
		debug_assert (false && "nano::transport::socket::async_read called with incorrect buffer size");
		boost::system::error_code ec_buffer = boost::system::errc::make_error_code (boost::system::errc::no_buffer_space);
		callback_a (ec_buffer, 0);
	}
}

void nano::transport::socket::async_write (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a)
{
	if (closed)
	{
		if (callback_a)
		{
			node.background ([callback = std::move (callback_a)] () {
				callback (boost::system::errc::make_error_code (boost::system::errc::not_supported), 0);
			});
		}

		return;
	}

	++queue_size;

	boost::asio::post (strand, boost::asio::bind_executor (strand, [buffer_a, callback = std::move (callback_a), this_l = shared_from_this ()] () mutable {
		if (this_l->closed)
		{
			if (callback)
			{
				callback (boost::system::errc::make_error_code (boost::system::errc::not_supported), 0);
			}

			return;
		}

		this_l->set_default_timeout ();

		nano::async_write (this_l->tcp_socket, buffer_a,
		boost::asio::bind_executor (this_l->strand,
		[buffer_a, cbk = std::move (callback), this_l] (boost::system::error_code ec, std::size_t size_a) {
			--this_l->queue_size;

			if (ec)
			{
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_write_error, nano::stat::dir::in);
				this_l->close ();
			}
			else
			{
				this_l->node.stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::out, size_a);
				this_l->set_last_completion ();
			}

			if (cbk)
			{
				cbk (ec, size_a);
			}
		}));
	}));
}

/** Call set_timeout with default_timeout as parameter */
void nano::transport::socket::set_default_timeout ()
{
	set_timeout (default_timeout);
}

/** Set the current timeout of the socket in seconds
 *  timeout occurs when the last socket completion is more than timeout seconds in the past
 *  timeout always applies, the socket always has a timeout
 *  to set infinite timeout, use std::numeric_limits<uint64_t>::max ()
 *  the function checkup() checks for timeout on a regular interval
 */
void nano::transport::socket::set_timeout (std::chrono::seconds timeout_a)
{
	timeout = timeout_a.count ();
}

void nano::transport::socket::set_last_completion ()
{
	last_completion_time_or_init = nano::seconds_since_epoch ();
}

void nano::transport::socket::set_last_receive_time ()
{
	last_receive_time_or_init = nano::seconds_since_epoch ();
}

void nano::transport::socket::checkup ()
{
	std::weak_ptr<nano::transport::socket> this_w (shared_from_this ());
	node.workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::seconds (node.network_params.network.is_dev_network () ? 1 : 5), [this_w] () {
		if (auto this_l = this_w.lock ())
		{
			// If the socket is already dead, close just in case, and stop doing checkups
			if (!this_l->alive ())
			{
				this_l->close ();
				return;
			}

			uint64_t now (nano::seconds_since_epoch ());
			auto condition_to_disconnect{ false };

			// if this is a server socket, and no data is received for silent_connection_tolerance_time seconds then disconnect
			if (this_l->endpoint_type () == endpoint_type_t::server && (now - this_l->last_receive_time_or_init) > static_cast<uint64_t> (this_l->silent_connection_tolerance_time.count ()))
			{
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_silent_connection_drop, nano::stat::dir::in);
				condition_to_disconnect = true;
			}

			// if there is no activity for timeout seconds then disconnect
			if ((now - this_l->last_completion_time_or_init) > this_l->timeout)
			{
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop,
				this_l->endpoint_type () == endpoint_type_t::server ? nano::stat::dir::in : nano::stat::dir::out);
				condition_to_disconnect = true;
			}

			if (condition_to_disconnect)
			{
				if (this_l->node.config.logging.network_timeout_logging ())
				{
					this_l->node.logger.try_log (boost::str (boost::format ("Disconnecting from %1% due to timeout") % this_l->remote));
				}
				this_l->timed_out = true;
				this_l->close ();
			}
			else if (!this_l->closed)
			{
				this_l->checkup ();
			}
		}
	});
}

bool nano::transport::socket::has_timed_out () const
{
	return timed_out;
}

void nano::transport::socket::set_default_timeout_value (std::chrono::seconds timeout_a)
{
	default_timeout = timeout_a;
}

std::chrono::seconds nano::transport::socket::get_default_timeout_value () const
{
	return default_timeout;
}

void nano::transport::socket::set_silent_connection_tolerance_time (std::chrono::seconds tolerance_time_a)
{
	auto this_l (shared_from_this ());
	boost::asio::dispatch (strand, boost::asio::bind_executor (strand, [this_l, tolerance_time_a] () {
		this_l->silent_connection_tolerance_time = tolerance_time_a;
	}));
}

void nano::transport::socket::close ()
{
	auto this_l (shared_from_this ());
	boost::asio::dispatch (strand, boost::asio::bind_executor (strand, [this_l] {
		this_l->close_internal ();
	}));
}

// This must be called from a strand or the destructor
void nano::transport::socket::close_internal ()
{
	if (!closed.exchange (true))
	{
		default_timeout = std::chrono::seconds (0);
		boost::system::error_code ec;

		// Ignore error code for shutdown as it is best-effort
		tcp_socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both, ec);
		tcp_socket.close (ec);
		if (ec)
		{
			node.logger.try_log ("Failed to close socket gracefully: ", ec.message ());
			node.stats.inc (nano::stat::type::bootstrap, nano::stat::detail::error_socket_close);
		}
	}
}

nano::tcp_endpoint nano::transport::socket::remote_endpoint () const
{
	return remote;
}

nano::tcp_endpoint nano::transport::socket::local_endpoint () const
{
	return tcp_socket.local_endpoint ();
}

nano::transport::server_socket::server_socket (nano::node & node_a, boost::asio::ip::tcp::endpoint local_a, std::size_t max_connections_a) :
	socket{ node_a, endpoint_type_t::server },
	acceptor{ node_a.io_ctx },
	local{ std::move (local_a) },
	max_inbound_connections{ max_connections_a }
{
	default_timeout = std::chrono::seconds::max ();
}

void nano::transport::server_socket::start (boost::system::error_code & ec_a)
{
	acceptor.open (local.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
	acceptor.bind (local, ec_a);
	if (!ec_a)
	{
		acceptor.listen (boost::asio::socket_base::max_listen_connections, ec_a);
	}
}

void nano::transport::server_socket::close ()
{
	auto this_l (std::static_pointer_cast<nano::transport::server_socket> (shared_from_this ()));

	boost::asio::dispatch (strand, boost::asio::bind_executor (strand, [this_l] () {
		this_l->close_internal ();
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

boost::asio::ip::network_v6 nano::transport::socket_functions::get_ipv6_subnet_address (boost::asio::ip::address_v6 const & ip_address, size_t network_prefix)
{
	return boost::asio::ip::make_network_v6 (ip_address, network_prefix);
}

boost::asio::ip::address nano::transport::socket_functions::first_ipv6_subnet_address (boost::asio::ip::address_v6 const & ip_address, size_t network_prefix)
{
	auto range = get_ipv6_subnet_address (ip_address, network_prefix).hosts ();
	debug_assert (!range.empty ());
	return *(range.begin ());
}

boost::asio::ip::address nano::transport::socket_functions::last_ipv6_subnet_address (boost::asio::ip::address_v6 const & ip_address, size_t network_prefix)
{
	auto range = get_ipv6_subnet_address (ip_address, network_prefix).hosts ();
	debug_assert (!range.empty ());
	return *(--range.end ());
}

size_t nano::transport::socket_functions::count_subnetwork_connections (
nano::transport::address_socket_mmap const & per_address_connections,
boost::asio::ip::address_v6 const & remote_address,
size_t network_prefix)
{
	auto range = get_ipv6_subnet_address (remote_address, network_prefix).hosts ();
	if (range.empty ())
	{
		return 0;
	}
	auto const first_ip = first_ipv6_subnet_address (remote_address, network_prefix);
	auto const last_ip = last_ipv6_subnet_address (remote_address, network_prefix);
	auto const counted_connections = std::distance (per_address_connections.lower_bound (first_ip), per_address_connections.upper_bound (last_ip));
	return counted_connections;
}

bool nano::transport::server_socket::limit_reached_for_incoming_subnetwork_connections (std::shared_ptr<nano::transport::socket> const & new_connection)
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

bool nano::transport::server_socket::limit_reached_for_incoming_ip_connections (std::shared_ptr<nano::transport::socket> const & new_connection)
{
	debug_assert (strand.running_in_this_thread ());
	if (node.flags.disable_max_peers_per_ip)
	{
		// If the limit is disabled, then it is unreachable.
		return false;
	}
	auto const address_connections_range = connections_per_address.equal_range (new_connection->remote.address ());
	auto const counted_connections = std::distance (address_connections_range.first, address_connections_range.second);
	return counted_connections >= node.network_params.network.max_peers_per_ip;
}

void nano::transport::server_socket::on_connection (std::function<bool (std::shared_ptr<nano::transport::socket> const &, boost::system::error_code const &)> callback_a)
{
	auto this_l (std::static_pointer_cast<nano::transport::server_socket> (shared_from_this ()));

	boost::asio::post (strand, boost::asio::bind_executor (strand, [this_l, callback = std::move (callback_a)] () mutable {
		if (!this_l->acceptor.is_open ())
		{
			this_l->node.logger.always_log ("Network: Acceptor is not open");
			return;
		}

		// Prepare new connection
		auto new_connection = std::make_shared<nano::transport::socket> (this_l->node, endpoint_type_t::server);
		this_l->acceptor.async_accept (new_connection->tcp_socket, new_connection->remote,
		boost::asio::bind_executor (this_l->strand,
		[this_l, new_connection, cbk = std::move (callback)] (boost::system::error_code const & ec_a) mutable {
			this_l->evict_dead_connections ();

			if (this_l->connections_per_address.size () >= this_l->max_inbound_connections)
			{
				this_l->node.logger.try_log ("Network: max_inbound_connections reached, unable to open new connection");
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_accept_failure, nano::stat::dir::in);
				this_l->on_connection_requeue_delayed (std::move (cbk));
				return;
			}

			if (this_l->limit_reached_for_incoming_ip_connections (new_connection))
			{
				auto const remote_ip_address = new_connection->remote_endpoint ().address ();
				auto const log_message = boost::str (
				boost::format ("Network: max connections per IP (max_peers_per_ip) was reached for %1%, unable to open new connection")
				% remote_ip_address.to_string ());
				this_l->node.logger.try_log (log_message);
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_ip, nano::stat::dir::in);
				this_l->on_connection_requeue_delayed (std::move (cbk));
				return;
			}

			if (this_l->limit_reached_for_incoming_subnetwork_connections (new_connection))
			{
				auto const remote_ip_address = new_connection->remote_endpoint ().address ();
				debug_assert (remote_ip_address.is_v6 ());
				auto const remote_subnet = socket_functions::get_ipv6_subnet_address (remote_ip_address.to_v6 (), this_l->node.network_params.network.max_peers_per_subnetwork);
				auto const log_message = boost::str (
				boost::format ("Network: max connections per subnetwork (max_peers_per_subnetwork) was reached for subnetwork %1% (remote IP: %2%), unable to open new connection")
				% remote_subnet.canonical ().to_string ()
				% remote_ip_address.to_string ());
				this_l->node.logger.try_log (log_message);
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_subnetwork, nano::stat::dir::in);
				this_l->on_connection_requeue_delayed (std::move (cbk));
				return;
			}

			if (!ec_a)
			{
				// Make sure the new connection doesn't idle. Note that in most cases, the callback is going to start
				// an IO operation immediately, which will start a timer.
				new_connection->checkup ();
				new_connection->set_timeout (this_l->node.network_params.network.idle_timeout);
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_accept_success, nano::stat::dir::in);
				this_l->connections_per_address.emplace (new_connection->remote.address (), new_connection);
				this_l->node.observers.socket_accepted.notify (*new_connection);
				if (cbk (new_connection, ec_a))
				{
					this_l->on_connection (std::move (cbk));
					return;
				}
				this_l->node.logger.always_log ("Network: Stopping to accept connections");
				return;
			}

			// accept error
			this_l->node.logger.try_log ("Network: Unable to accept connection: ", ec_a.message ());
			this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_accept_failure, nano::stat::dir::in);

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
			this_l->node.logger.always_log ("Network: Stopping to accept connections");
		}));
	}));
}

// If we are unable to accept a socket, for any reason, we wait just a little (1ms) before rescheduling the next connection accept.
// The intention is to throttle back the connection requests and break up any busy loops that could possibly form and
// give the rest of the system a chance to recover.
void nano::transport::server_socket::on_connection_requeue_delayed (std::function<bool (std::shared_ptr<nano::transport::socket> const &, boost::system::error_code const &)> callback_a)
{
	auto this_l (std::static_pointer_cast<nano::transport::server_socket> (shared_from_this ()));
	node.workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::milliseconds (1), [this_l, callback = std::move (callback_a)] () mutable {
		this_l->on_connection (std::move (callback));
	});
}

// This must be called from a strand
void nano::transport::server_socket::evict_dead_connections ()
{
	debug_assert (strand.running_in_this_thread ());
	for (auto it = connections_per_address.begin (); it != connections_per_address.end ();)
	{
		if (it->second.expired ())
		{
			it = connections_per_address.erase (it);
			continue;
		}
		++it;
	}
}

std::string nano::transport::socket_type_to_string (nano::transport::socket::type_t type)
{
	switch (type)
	{
		case nano::transport::socket::type_t::undefined:
			return "undefined";
		case nano::transport::socket::type_t::bootstrap:
			return "bootstrap";
		case nano::transport::socket::type_t::realtime:
			return "realtime";
		case nano::transport::socket::type_t::realtime_response_server:
			return "realtime_response_server";
	}
	return "n/a";
}
