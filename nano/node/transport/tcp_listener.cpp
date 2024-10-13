#include <nano/lib/enum_util.hpp>
#include <nano/lib/interval.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_server.hpp>

#include <boost/asio/use_future.hpp>

#include <memory>
#include <ranges>

using namespace std::chrono_literals;

/*
 * tcp_listener
 */

nano::transport::tcp_listener::tcp_listener (uint16_t port_a, tcp_config const & config_a, nano::node & node_a) :
	config{ config_a },
	node{ node_a },
	stats{ node_a.stats },
	logger{ node_a.logger },
	port{ port_a },
	strand{ node_a.io_ctx.get_executor () },
	acceptor{ strand },
	task{ strand }
{
	connection_accepted.add ([this] (auto const & socket, auto const & server) {
		node.observers.socket_connected.notify (*socket);
	});
}

nano::transport::tcp_listener::~tcp_listener ()
{
	debug_assert (!cleanup_thread.joinable ());
	debug_assert (!task.joinable ());
	debug_assert (connection_count () == 0);
	debug_assert (attempt_count () == 0);
}

void nano::transport::tcp_listener::start ()
{
	debug_assert (!cleanup_thread.joinable ());
	debug_assert (!task.joinable ());

	try
	{
		asio::ip::tcp::endpoint target{ asio::ip::address_v6::any (), port };

		acceptor.open (target.protocol ());
		acceptor.set_option (asio::ip::tcp::acceptor::reuse_address (true));
		acceptor.bind (target);
		acceptor.listen (asio::socket_base::max_listen_connections);

		{
			std::lock_guard<nano::mutex> lock{ mutex };
			local = acceptor.local_endpoint ();
		}

		logger.debug (nano::log::type::tcp_listener, "Listening for incoming connections on: {}", fmt::streamed (acceptor.local_endpoint ()));
	}
	catch (boost::system::system_error const & ex)
	{
		logger.critical (nano::log::type::tcp_listener, "Error while binding for incoming TCP: {} (port: {})", ex.what (), port);
		throw;
	}

	task = nano::async::task (strand, [this] () -> asio::awaitable<void> {
		try
		{
			logger.debug (nano::log::type::tcp_listener, "Starting acceptor");

			try
			{
				co_await run ();
			}
			catch (boost::system::system_error const & ex)
			{
				// Operation aborted is expected when cancelling the acceptor
				debug_assert (ex.code () == asio::error::operation_aborted);
			}
			debug_assert (strand.running_in_this_thread ());

			logger.debug (nano::log::type::tcp_listener, "Stopped acceptor");
		}
		catch (std::exception const & ex)
		{
			logger.critical (nano::log::type::tcp_listener, "Error: {}", ex.what ());
			release_assert (false); // Unexpected error
		}
		catch (...)
		{
			logger.critical (nano::log::type::tcp_listener, "Unknown error");
			release_assert (false); // Unexpected error
		}
	});

	cleanup_thread = std::thread ([this] {
		nano::thread_role::set (nano::thread_role::name::tcp_listener);
		run_cleanup ();
	});
}

void nano::transport::tcp_listener::stop ()
{
	debug_assert (!stopped);

	logger.debug (nano::log::type::tcp_listener, "Stopping listening for incoming connections and closing all sockets...");

	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
		local = {};
	}
	condition.notify_all ();

	if (task.joinable ())
	{
		task.cancel ();
		task.join ();
	}
	if (cleanup_thread.joinable ())
	{
		cleanup_thread.join ();
	}

	boost::system::error_code ec;
	acceptor.close (ec); // Best effort to close the acceptor, ignore errors
	if (ec)
	{
		logger.error (nano::log::type::tcp_listener, "Error while closing acceptor: {}", ec.message ());
	}

	decltype (connections) connections_l;
	decltype (attempts) attempts_l;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		connections_l.swap (connections);
		attempts_l.swap (attempts);
	}

	for (auto & attempt : attempts_l)
	{
		debug_assert (attempt.task.joinable ());
		attempt.task.cancel ();
		attempt.task.join ();
	}

	for (auto & connection : connections_l)
	{
		if (auto socket = connection.socket.lock ())
		{
			socket->close ();
		}
		if (auto server = connection.server.lock ())
		{
			server->stop ();
		}
	}
}

void nano::transport::tcp_listener::run_cleanup ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::cleanup);

		cleanup ();
		timeout ();

		condition.wait_for (lock, 1s, [this] () { return stopped.load (); });
	}
}

void nano::transport::tcp_listener::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	// Erase dead connections
	erase_if (connections, [this] (auto const & connection) {
		if (connection.socket.expired () && connection.server.expired ())
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::erase_dead);
			logger.debug (nano::log::type::tcp_listener, "Evicting dead connection: {}", fmt::streamed (connection.endpoint));
			return true;
		}
		else
		{
			return false;
		}
	});

	// Erase completed attempts
	erase_if (attempts, [this] (auto const & attempt) {
		return attempt.task.ready ();
	});
}

void nano::transport::tcp_listener::timeout ()
{
	debug_assert (!mutex.try_lock ());

	auto const cutoff = std::chrono::steady_clock::now () - config.connect_timeout;

	// Cancel timed out attempts
	for (auto & attempt : attempts)
	{
		if (!attempt.task.ready () && attempt.start < cutoff)
		{
			attempt.task.cancel ();

			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::attempt_timeout);
			logger.debug (nano::log::type::tcp_listener, "Connection attempt timed out: {} (started {}s ago)",
			fmt::streamed (attempt.endpoint), nano::log::seconds_delta (attempt.start));
		}
	}
}

bool nano::transport::tcp_listener::connect (asio::ip::address ip, uint16_t port)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	if (stopped)
	{
		return false; // Rejected
	}

	if (port == 0)
	{
		port = node.network_params.network.default_node_port;
	}

	if (auto count = attempts.size (); count > config.max_attempts)
	{
		stats.inc (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_attempts, nano::stat::dir::out);
		logger.debug (nano::log::type::tcp_listener, "Max connection attempts reached ({}), rejected connection attempt: {}",
		count, ip.to_string ());

		return false; // Rejected
	}

	if (auto count = count_attempts (ip); count >= config.max_attempts_per_ip)
	{
		stats.inc (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_attempts_per_ip, nano::stat::dir::out);
		logger.debug (nano::log::type::tcp_listener, "Connection attempt already in progress ({}), rejected connection attempt: {}",
		count, ip.to_string ());

		return false; // Rejected
	}

	if (auto result = check_limits (ip, connection_type::outbound); result != accept_result::accepted)
	{
		stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::connect_rejected, nano::stat::dir::out);
		// Refusal reason should be logged earlier

		return false; // Rejected
	}

	nano::tcp_endpoint const endpoint{ ip, port };

	stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::connect_initiate, nano::stat::dir::out);
	logger.debug (nano::log::type::tcp_listener, "Initiating outgoing connection to: {}", fmt::streamed (endpoint));

	auto task = nano::async::task (strand, connect_impl (endpoint));

	attempts.emplace_back (attempt{ endpoint, std::move (task) });

	return true; // Attempt started
}

auto nano::transport::tcp_listener::connect_impl (asio::ip::tcp::endpoint endpoint) -> asio::awaitable<void>
{
	debug_assert (strand.running_in_this_thread ());

	try
	{
		auto raw_socket = co_await connect_socket (endpoint);
		debug_assert (strand.running_in_this_thread ());

		auto result = accept_one (std::move (raw_socket), connection_type::outbound);
		if (result.result == accept_result::accepted)
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::connect_success, nano::stat::dir::out);
			logger.debug (nano::log::type::tcp_listener, "Successfully connected to: {}", fmt::streamed (endpoint));

			release_assert (result.server);
			result.server->initiate_handshake ();
		}
		else
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::connect_failure, nano::stat::dir::out);
			// Refusal reason should be logged earlier
		}
	}
	catch (boost::system::system_error const & ex)
	{
		stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::connect_error, nano::stat::dir::out);
		logger.log (nano::log::level::debug, nano::log::type::tcp_listener, "Error connecting to: {} ({})", fmt::streamed (endpoint), ex.what ());
	}
}

asio::awaitable<void> nano::transport::tcp_listener::run ()
{
	debug_assert (strand.running_in_this_thread ());

	while (!stopped && acceptor.is_open ())
	{
		co_await wait_available_slots ();

		try
		{
			auto socket = co_await accept_socket ();
			debug_assert (strand.running_in_this_thread ());

			auto result = accept_one (std::move (socket), connection_type::inbound);
			if (result.result != accept_result::accepted)
			{
				stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::accept_failure, nano::stat::dir::in);
				// Refusal reason should be logged earlier
			}
		}
		catch (boost::system::system_error const & ex)
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::accept_error, nano::stat::dir::in);
			logger.log (nano::log::level::debug, nano::log::type::tcp_listener, "Error accepting incoming connection: {}", ex.what ());
		}

		// Sleep for a while to prevent busy loop
		co_await nano::async::sleep_for (10ms);
	}
	if (!stopped)
	{
		logger.error (nano::log::type::tcp_listener, "Acceptor stopped unexpectedly");
		debug_assert (false, "acceptor stopped unexpectedly");
	}
}

asio::awaitable<asio::ip::tcp::socket> nano::transport::tcp_listener::accept_socket ()
{
	debug_assert (strand.running_in_this_thread ());

	co_return co_await acceptor.async_accept (asio::use_awaitable);
}

asio::awaitable<asio::ip::tcp::socket> nano::transport::tcp_listener::connect_socket (asio::ip::tcp::endpoint endpoint)
{
	debug_assert (strand.running_in_this_thread ());

	asio::ip::tcp::socket raw_socket{ strand };
	co_await raw_socket.async_connect (endpoint, asio::use_awaitable);

	co_return raw_socket;
}

asio::awaitable<void> nano::transport::tcp_listener::wait_available_slots () const
{
	nano::interval log_interval;
	while (connection_count () >= config.max_inbound_connections && !stopped)
	{
		if (log_interval.elapsed (node.network_params.network.is_dev_network () ? 1s : 15s))
		{
			logger.warn (nano::log::type::tcp_listener, "Waiting for available slots to accept new connections (current: {} / max: {})",
			connection_count (), config.max_inbound_connections);
		}

		co_await nano::async::sleep_for (100ms);
	}
}

auto nano::transport::tcp_listener::accept_one (asio::ip::tcp::socket raw_socket, connection_type type) -> accept_return
{
	auto const remote_endpoint = raw_socket.remote_endpoint ();
	auto const local_endpoint = raw_socket.local_endpoint ();

	nano::unique_lock<nano::mutex> lock{ mutex };

	if (stopped)
	{
		return { accept_result::rejected };
	}

	if (auto result = check_limits (remote_endpoint.address (), type); result != accept_result::accepted)
	{
		stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::accept_rejected, to_stat_dir (type));
		logger.debug (nano::log::type::tcp_listener, "Rejected connection from: {} ({})", fmt::streamed (remote_endpoint), to_string (type));
		// Rejection reason should be logged earlier

		try
		{
			// Best effort attempt to gracefully close the socket, shutdown before closing to avoid zombie sockets
			raw_socket.shutdown (asio::ip::tcp::socket::shutdown_both);
			raw_socket.close ();
		}
		catch (boost::system::system_error const & ex)
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::close_error, to_stat_dir (type));
			logger.debug (nano::log::type::tcp_listener, "Error while closing socket after refusing connection: {} ({})", ex.what (), to_string (type));
		}

		return { result };
	}

	stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::accept_success, to_stat_dir (type));
	logger.debug (nano::log::type::tcp_listener, "Accepted connection: {} ({})", fmt::streamed (remote_endpoint), to_string (type));

	auto socket = std::make_shared<nano::transport::tcp_socket> (node, std::move (raw_socket), remote_endpoint, local_endpoint, to_socket_endpoint (type));
	auto server = std::make_shared<nano::transport::tcp_server> (socket, node.shared (), true);

	connections.emplace_back (connection{ remote_endpoint, socket, server });

	lock.unlock ();

	socket->set_timeout (node.network_params.network.idle_timeout);
	socket->start ();
	server->start ();

	connection_accepted.notify (socket, server);

	return { accept_result::accepted, socket, server };
}

auto nano::transport::tcp_listener::check_limits (asio::ip::address const & ip, connection_type type) -> accept_result
{
	debug_assert (!mutex.try_lock ());

	if (stopped)
	{
		return accept_result::rejected;
	}

	cleanup ();

	if (node.network.excluded_peers.check (ip)) // true => error
	{
		stats.inc (nano::stat::type::tcp_listener_rejected, nano::stat::detail::excluded, to_stat_dir (type));
		logger.debug (nano::log::type::tcp_listener, "Rejected connection from excluded peer: {}", ip.to_string ());

		return accept_result::rejected;
	}

	if (!node.flags.disable_max_peers_per_ip)
	{
		if (auto count = count_per_ip (ip); count >= node.config.network.max_peers_per_ip)
		{
			stats.inc (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip, to_stat_dir (type));
			logger.debug (nano::log::type::tcp_listener, "Max connections per IP reached ({}), unable to open new connection: {}",
			count, ip.to_string ());

			return accept_result::rejected;
		}
	}

	// If the address is IPv4 we don't check for a network limit, since its address space isn't big as IPv6/64.
	if (!node.flags.disable_max_peers_per_subnetwork && !nano::transport::is_ipv4_or_v4_mapped_address (ip))
	{
		if (auto count = count_per_subnetwork (ip); count >= node.config.network.max_peers_per_subnetwork)
		{
			stats.inc (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_subnetwork, to_stat_dir (type));
			logger.debug (nano::log::type::tcp_listener, "Max connections per subnetwork reached ({}), unable to open new connection: {}",
			count, ip.to_string ());

			return accept_result::rejected;
		}
	}

	if (type == connection_type::inbound)
	{
		debug_assert (connections.size () <= config.max_inbound_connections); // Should be checked earlier (wait_available_slots)

		if (auto count = count_per_type (connection_type::inbound); count >= config.max_inbound_connections)
		{
			stats.inc (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_attempts, to_stat_dir (type));
			logger.debug (nano::log::type::tcp_listener, "Max inbound connections reached ({}), unable to accept new connection: {}",
			count, ip.to_string ());

			return accept_result::rejected;
		}
	}
	if (type == connection_type::outbound)
	{
		if (auto count = count_per_type (connection_type::outbound); count >= config.max_outbound_connections)
		{
			stats.inc (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_attempts, to_stat_dir (type));
			logger.debug (nano::log::type::tcp_listener, "Max outbound connections reached ({}), unable to initiate new connection: {}",
			count, ip.to_string ());

			return accept_result::rejected;
		}
	}

	return accept_result::accepted;
}

size_t nano::transport::tcp_listener::connection_count () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return connections.size ();
}

size_t nano::transport::tcp_listener::connection_count (connection_type type) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return count_per_type (type);
}

size_t nano::transport::tcp_listener::attempt_count () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return attempts.size ();
}

size_t nano::transport::tcp_listener::realtime_count () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	return std::count_if (connections.begin (), connections.end (), [] (auto const & connection) {
		if (auto socket = connection.socket.lock ())
		{
			return socket->is_realtime_connection ();
		}
		return false;
	});
}

size_t nano::transport::tcp_listener::bootstrap_count () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	return std::count_if (connections.begin (), connections.end (), [] (auto const & connection) {
		if (auto socket = connection.socket.lock ())
		{
			return socket->is_bootstrap_connection ();
		}
		return false;
	});
}

size_t nano::transport::tcp_listener::count_per_type (connection_type type) const
{
	debug_assert (!mutex.try_lock ());

	return std::count_if (connections.begin (), connections.end (), [type] (auto const & connection) {
		if (auto socket = connection.socket.lock ())
		{
			return socket->endpoint_type () == to_socket_endpoint (type);
		}
		return false;
	});
}

size_t nano::transport::tcp_listener::count_per_ip (asio::ip::address const & ip) const
{
	debug_assert (!mutex.try_lock ());

	return std::count_if (connections.begin (), connections.end (), [&ip] (auto const & connection) {
		return nano::transport::is_same_ip (connection.address (), ip);
	});
}

size_t nano::transport::tcp_listener::count_per_subnetwork (asio::ip::address const & ip) const
{
	debug_assert (!mutex.try_lock ());

	return std::count_if (connections.begin (), connections.end (), [this, &ip] (auto const & connection) {
		return nano::transport::is_same_subnetwork (connection.address (), ip);
	});
}

size_t nano::transport::tcp_listener::count_attempts (asio::ip::address const & ip) const
{
	debug_assert (!mutex.try_lock ());

	return std::count_if (attempts.begin (), attempts.end (), [&ip] (auto const & attempt) {
		return nano::transport::is_same_ip (attempt.address (), ip);
	});
}

asio::ip::tcp::endpoint nano::transport::tcp_listener::endpoint () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return { asio::ip::address_v6::loopback (), local.port () };
}

auto nano::transport::tcp_listener::sockets () const -> std::vector<std::shared_ptr<tcp_socket>>
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto r = connections
	| std::views::transform ([] (auto const & connection) { return connection.socket.lock (); })
	| std::views::filter ([] (auto const & socket) { return socket != nullptr; });
	return { r.begin (), r.end () };
}

auto nano::transport::tcp_listener::servers () const -> std::vector<std::shared_ptr<tcp_server>>
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto r = connections
	| std::views::transform ([] (auto const & connection) { return connection.server.lock (); })
	| std::views::filter ([] (auto const & server) { return server != nullptr; });
	return { r.begin (), r.end () };
}

nano::container_info nano::transport::tcp_listener::container_info () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	nano::container_info info;
	info.put ("connections", connections.size ());
	info.put ("attempts", attempts.size ());
	return info;
}

/*
 *
 */

nano::stat::dir nano::transport::tcp_listener::to_stat_dir (connection_type type)
{
	switch (type)
	{
		case connection_type::inbound:
			return nano::stat::dir::in;
		case connection_type::outbound:
			return nano::stat::dir::out;
	}
	debug_assert (false);
	return {};
}

std::string_view nano::transport::tcp_listener::to_string (connection_type type)
{
	return nano::enum_util::name (type);
}

nano::transport::socket_endpoint nano::transport::tcp_listener::to_socket_endpoint (connection_type type)
{
	switch (type)
	{
		case connection_type::inbound:
			return socket_endpoint::server;
		case connection_type::outbound:
			return socket_endpoint::client;
	}
	debug_assert (false);
	return {};
}
