#include <nano/lib/enum_util.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/interval.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/network_formatting.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/peer_blocklist.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_server.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/asio/use_future.hpp>

#include <algorithm>
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
	index{ index_limits (config_a, node_a) },
	strand{ node_a.io_ctx.get_executor () },
	acceptor{ strand },
	task{ strand }
{
	connection_accepted.add ([this] (auto const & socket, auto const & server) {
		node.observers.socket_connected.notify (socket);
	});
}

auto nano::transport::tcp_listener::index_limits (tcp_config const & config, nano::node const & node) -> tcp_listener_index::limits
{
	tcp_listener_index::limits limits;
	limits.max_inbound = config.max_inbound_connections;
	limits.max_outbound = config.max_outbound_connections;
	limits.max_attempts = config.max_attempts;
	limits.max_attempts_per_ip = config.max_attempts_per_ip;
	limits.max_per_ip = node.flags.disable_max_peers_per_ip ? std::optional<size_t>{} : std::optional<size_t>{ node.config.network->max_peers_per_ip };
	limits.max_per_subnetwork = node.flags.disable_max_peers_per_subnetwork ? std::optional<size_t>{} : std::optional<size_t>{ node.config.network->max_peers_per_subnetwork };
	limits.reconnect_cooldown = config.reconnect_cooldown;
	return limits;
}

nano::transport::tcp_listener::~tcp_listener ()
{
	debug_assert (!cleanup_thread.joinable ());
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

#ifndef _WIN32
		// On Unix, SO_REUSEADDR allows binding to a port in TIME_WAIT state,
		// but still fails if the port is actively bound by another socket.
		// On Windows, SO_REUSEADDR has different semantics - it allows multiple
		// sockets to bind to the same port (port stealing), so we skip it there.
		acceptor.set_option (asio::ip::tcp::acceptor::reuse_address (true));
#endif

		acceptor.bind (target);
		acceptor.listen (asio::socket_base::max_listen_connections);

		{
			std::lock_guard<nano::mutex> lock{ mutex };
			local = acceptor.local_endpoint ();
		}

		logger.debug (nano::log::type::tcp_listener, "Listening for incoming connections on: {}", acceptor.local_endpoint ());
	}
	catch (boost::system::system_error const & ex)
	{
		logger.critical (nano::log::type::tcp_listener, "Error while binding for incoming TCP: {} (port: {})", ex.code ().message (), port);
		throw;
	}

	task = nano::async::task (strand, start_impl ());

	cleanup_thread = std::thread ([this] {
		nano::thread_role::set (nano::thread_role::name::tcp_listener);
		run_cleanup ();
	});
}

asio::awaitable<void> nano::transport::tcp_listener::start_impl ()
{
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
}

void nano::transport::tcp_listener::stop ()
{
	debug_assert (!stopped);

	logger.debug (nano::log::type::tcp_listener, "Stopping...");

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

	logger.debug (nano::log::type::tcp_listener, "Closing all sockets...");

	tcp_listener_index::contents contents_l;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		contents_l = index.clear ();
	}

	for (auto const & attempt : contents_l.attempts)
	{
		debug_assert (attempt.task->joinable ());
		attempt.task->cancel ();
		attempt.task->join ();
	}

	for (auto const & connection : contents_l.connections)
	{
		connection.socket->close ();
		connection.server->close ();
	}

	logger.debug (nano::log::type::tcp_listener, "Stopped");
}

void nano::transport::tcp_listener::run_cleanup ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::cleanup);

		timeout ();
		purge (lock);
		debug_assert (!lock.owns_lock ());
		lock.lock ();

		condition.wait_for (lock, 1s, [this] () { return stopped.load (); });
	}
}

void nano::transport::tcp_listener::purge (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	auto const now = std::chrono::steady_clock::now ();

	// Erase completed attempts, released once the lock is gone
	auto erased_attempts = index.erase_attempts_if ([] (auto const & attempt) {
		return attempt.task->ready ();
	});

	// Erase dead connections, their cooldowns start now
	auto erased_connections = index.erase_if ([] (auto const & connection) {
		return !connection.socket->alive ();
	},
	now);

	index.update (now);

	lock.unlock ();

	for (auto const & connection : erased_connections)
	{
		stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::erase_dead);
		logger.debug (nano::log::type::tcp_listener, "Evicting dead connection: {}", connection.endpoint);

		connection.socket->close ();
		connection.server->close ();
	}
}

void nano::transport::tcp_listener::timeout ()
{
	debug_assert (!mutex.try_lock ());

	auto const now = std::chrono::steady_clock::now ();
	auto const connect_cutoff = now - config.connect_timeout;
	auto const handshake_cutoff = now - config.handshake_timeout;

	// Cancel timed out attempts
	for (auto const & attempt : index.list_attempts ())
	{
		if (!attempt.task->ready () && attempt.start < connect_cutoff)
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::attempt_timeout);
			logger.debug (nano::log::type::tcp_listener, "Connection attempt timed out: {} (started {}s ago)",
			attempt.endpoint,
			nano::log::seconds_delta (attempt.start));

			attempt.task->cancel (); // Cancel is non-blocking and will return immediately, safe to call under lock
		}
	}

	// Cancel too long handshakes
	for (auto const & connection : index.list_connections ())
	{
		if (connection.socket->type () == nano::transport::socket_type::undefined && connection.socket->get_time_connected () < handshake_cutoff)
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::handshake_timeout);
			logger.debug (nano::log::type::tcp_listener, "Handshake timed out: {} (connected {}s ago)",
			connection.endpoint,
			nano::log::seconds_delta (connection.socket->get_time_connected ()));

			connection.socket->close_async (); // Schedule socket close, this is non-blocking, safe to call under lock
		}
	}
}

bool nano::transport::tcp_listener::connect (asio::ip::address ip, uint16_t port, nano::transport::connect_callback callback)
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

	if (auto result = index.check_attempt (ip); result != tcp_listener_index::result::accepted)
	{
		stats.inc (nano::stat::type::tcp_listener_rejected, to_stat_detail (result), nano::stat::dir::out);
		logger.debug (nano::log::type::tcp_listener, "Attempt limit reached: {}, rejected connection attempt: {}", to_string (result), ip);

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
	logger.debug (nano::log::type::tcp_listener, "Initiating outgoing connection to: {}", endpoint);

	auto task = std::make_shared<nano::async::task> (strand, connect_impl (endpoint, std::move (callback)));
	index.insert_attempt (endpoint, task, std::chrono::steady_clock::now ());

	return true; // Attempt started
}

auto nano::transport::tcp_listener::connect_impl (asio::ip::tcp::endpoint endpoint, nano::transport::connect_callback callback) -> asio::awaitable<void>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (callback);

	try
	{
		auto raw_socket = co_await connect_socket (endpoint);
		debug_assert (strand.running_in_this_thread ());

		auto result = accept_one (std::move (raw_socket), connection_type::outbound);
		if (result.result == accept_result::accepted)
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::connect_success, nano::stat::dir::out);
			logger.debug (nano::log::type::tcp_listener, "Successfully connected to: {}", endpoint);

			callback (endpoint, {}); // No error
		}
		else
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::connect_failure, nano::stat::dir::out);

			// Report the specific local-rejection reason to the callback
			callback (endpoint, to_error_code (result.result));
		}
	}
	catch (boost::system::system_error const & ex)
	{
		stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::connect_error, nano::stat::dir::out);
		stats.inc (nano::stat::type::tcp_listener_connect_ec, nano::to_stat_detail (ex.code ()));
		logger.log (nano::log::level::debug, nano::log::type::tcp_listener, "Error connecting to: {} ({})", endpoint, ex.code ());

		callback (endpoint, ex.code ());
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
			stats.inc (nano::stat::type::tcp_listener_accept_ec, nano::to_stat_detail (ex.code ()));
			logger.log (nano::log::level::debug, nano::log::type::tcp_listener, "Error accepting incoming connection: {}", ex.code ());
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
	while (connection_count (connection_type::inbound) >= config.max_inbound_connections && !stopped)
	{
		if (log_interval.elapse (node.network_params.network.is_dev_network () ? 1s : 15s))
		{
			logger.warn (nano::log::type::tcp_listener, "Waiting for available slots to accept new connections (current: {} / max: {})",
			connection_count (connection_type::inbound), config.max_inbound_connections);
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
		logger.debug (nano::log::type::tcp_listener, "Rejected connection: {} reason: {} ({})",
		remote_endpoint,
		to_string (result),
		to_string (type));
		// Rejection details should be logged earlier

		try
		{
			// Best effort attempt to gracefully close the socket, shutdown before closing to avoid zombie sockets
			raw_socket.shutdown (asio::ip::tcp::socket::shutdown_both);
			raw_socket.close ();
		}
		catch (boost::system::system_error const & ex)
		{
			stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::close_error, to_stat_dir (type));
			logger.debug (nano::log::type::tcp_listener, "Error while closing socket after refusing connection: {} ({})",
			ex.code (), to_string (type));
		}

		return { result };
	}

	stats.inc (nano::stat::type::tcp_listener, nano::stat::detail::accept_success, to_stat_dir (type));
	logger.debug (nano::log::type::tcp_listener, "Accepted connection: {} ({})", remote_endpoint, type);

	auto socket = std::make_shared<nano::transport::tcp_socket> (node, std::move (raw_socket), to_socket_endpoint (type));
	auto server = std::make_shared<nano::transport::tcp_server> (node, socket);
	server->start ();

	index.insert (type, remote_endpoint, socket, server);

	lock.unlock ();

	connection_accepted.notify (socket, server);

	return { accept_result::accepted, socket, server };
}

auto nano::transport::tcp_listener::check_limits (asio::ip::address const & ip, connection_type type) const -> accept_result
{
	debug_assert (!mutex.try_lock ());

	if (stopped)
	{
		return accept_result::rejected;
	}

	if (node.peer_blocklist.contains (ip))
	{
		stats.inc (nano::stat::type::tcp_listener_rejected, nano::stat::detail::blocklisted, to_stat_dir (type));
		logger.debug (nano::log::type::tcp_listener, "Rejected connection with blocklisted peer: {} ({})", ip, type);

		return accept_result::rejected_blocklisted;
	}

	if (node.network.excluded_peers.check (ip)) // true => error
	{
		stats.inc (nano::stat::type::tcp_listener_rejected, nano::stat::detail::excluded, to_stat_dir (type));
		logger.debug (nano::log::type::tcp_listener, "Rejected connection from excluded peer: {} ({})", ip, type);

		return accept_result::rejected_excluded;
	}

	if (type == connection_type::inbound)
	{
		debug_assert (index.size (connection_type::inbound) <= config.max_inbound_connections); // Should be checked earlier (wait_available_slots)
	}

	if (auto result = index.check (type, ip); result != tcp_listener_index::result::accepted)
	{
		stats.inc (nano::stat::type::tcp_listener_rejected, to_stat_detail (result), to_stat_dir (type));
		logger.debug (nano::log::type::tcp_listener, "Connection limit reached: {}, unable to open a new connection: {} ({})", to_string (result), ip, to_string (type));

		return to_accept_result (result);
	}

	return accept_result::accepted;
}

size_t nano::transport::tcp_listener::connection_count () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return index.size ();
}

size_t nano::transport::tcp_listener::connection_count (connection_type type) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return index.size (type);
}

size_t nano::transport::tcp_listener::attempt_count () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return index.attempt_count ();
}

size_t nano::transport::tcp_listener::realtime_count () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto const connections = index.list_connections ();
	return std::count_if (connections.begin (), connections.end (), [] (auto const & connection) {
		return connection.socket->type () == nano::transport::socket_type::realtime;
	});
}

size_t nano::transport::tcp_listener::bootstrap_count () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto const connections = index.list_connections ();
	return std::count_if (connections.begin (), connections.end (), [] (auto const & connection) {
		return connection.socket->type () == nano::transport::socket_type::bootstrap;
	});
}

asio::ip::tcp::endpoint nano::transport::tcp_listener::endpoint () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return { asio::ip::address_v6::loopback (), local.port () };
}

auto nano::transport::tcp_listener::all_sockets () const -> std::deque<std::shared_ptr<tcp_socket>>
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto const connections = index.list_connections ();
	auto r = connections | std::views::transform ([] (auto const & connection) { return connection.socket; });
	return { r.begin (), r.end () };
}

auto nano::transport::tcp_listener::all_servers () const -> std::deque<std::shared_ptr<tcp_server>>
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto const connections = index.list_connections ();
	auto r = connections | std::views::transform ([] (auto const & connection) { return connection.server; });
	return { r.begin (), r.end () };
}

nano::container_info nano::transport::tcp_listener::container_info () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	return index.container_info ();
}

/*
 *
 */

nano::stat::dir nano::transport::to_stat_dir (nano::transport::connection_type type)
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

nano::transport::socket_endpoint nano::transport::to_socket_endpoint (nano::transport::connection_type type)
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

nano::transport::tcp_listener::accept_result nano::transport::to_accept_result (nano::transport::tcp_listener_index::result result)
{
	using accept_result = nano::transport::tcp_listener::accept_result;
	switch (result)
	{
		case tcp_listener_index::result::accepted:
			return accept_result::accepted;
		case tcp_listener_index::result::max_per_ip:
			return accept_result::rejected_max_per_ip;
		case tcp_listener_index::result::max_per_subnetwork:
			return accept_result::rejected_max_per_subnetwork;
		case tcp_listener_index::result::max_inbound:
			return accept_result::rejected_max_inbound;
		case tcp_listener_index::result::max_outbound:
			return accept_result::rejected_max_outbound;
		case tcp_listener_index::result::max_attempts:
		case tcp_listener_index::result::max_attempts_per_ip:
			break; // Attempt limits are settled before a connection is attempted
	}
	debug_assert (false);
	return accept_result::rejected;
}

std::string_view nano::transport::to_string (nano::transport::tcp_listener_index::result result)
{
	return nano::enum_to_string (result);
}

nano::stat::detail nano::transport::to_stat_detail (nano::transport::tcp_listener_index::result result)
{
	using result_t = nano::transport::tcp_listener_index::result;
	switch (result)
	{
		case result_t::max_per_ip:
			return nano::stat::detail::max_per_ip;
		case result_t::max_per_subnetwork:
			return nano::stat::detail::max_per_subnetwork;
		case result_t::max_inbound:
			return nano::stat::detail::max_inbound;
		case result_t::max_outbound:
			return nano::stat::detail::max_outbound;
		case result_t::max_attempts:
			return nano::stat::detail::max_attempts;
		case result_t::max_attempts_per_ip:
			return nano::stat::detail::max_attempts_per_ip;
		case result_t::accepted:
			break; // Nothing to count
	}
	debug_assert (false);
	return nano::stat::detail::all;
}

std::string_view nano::transport::to_string (nano::transport::tcp_listener::accept_result result)
{
	return nano::enum_to_string (result);
}

std::error_code nano::transport::to_error_code (nano::transport::tcp_listener::accept_result result)
{
	using accept_result = nano::transport::tcp_listener::accept_result;
	switch (result)
	{
		case accept_result::rejected_excluded:
			return nano::error_network::peer_excluded;
		case accept_result::rejected_blocklisted:
			return nano::error_network::peer_blocklisted;
		case accept_result::rejected_max_per_ip:
			return nano::error_network::max_connections_per_ip;
		case accept_result::rejected_max_per_subnetwork:
			return nano::error_network::max_connections_per_subnetwork;
		case accept_result::rejected_max_inbound:
			return nano::error_network::max_inbound_connections;
		case accept_result::rejected_max_outbound:
			return nano::error_network::max_outbound_connections;
		case accept_result::invalid:
		case accept_result::accepted:
		case accept_result::rejected:
		case accept_result::error:
			break;
	}
	return nano::error_network::connection_rejected_locally;
}

/*
 * tcp_listener_index
 */

nano::transport::tcp_listener_index::tcp_listener_index (limits const & limits_a) :
	config{ limits_a }
{
}

auto nano::transport::tcp_listener_index::check (connection_type type, asio::ip::address const & ip) const -> result
{
	if (config.max_per_ip && count_per_ip (ip) >= *config.max_per_ip)
	{
		return result::max_per_ip;
	}
	// The IPv4 address space is too small for a per subnetwork limit to make sense
	if (config.max_per_subnetwork && !nano::transport::is_ipv4_or_v4_mapped_address (ip) && count_per_subnetwork (ip) >= *config.max_per_subnetwork)
	{
		return result::max_per_subnetwork;
	}
	switch (type)
	{
		case connection_type::inbound:
		{
			if (size (connection_type::inbound) >= config.max_inbound)
			{
				return result::max_inbound;
			}
		}
		break;
		case connection_type::outbound:
		{
			if (size (connection_type::outbound) >= config.max_outbound)
			{
				return result::max_outbound;
			}
		}
		break;
	}
	return result::accepted;
}

auto nano::transport::tcp_listener_index::check_attempt (asio::ip::address const & ip) const -> result
{
	if (attempts.size () >= config.max_attempts)
	{
		return result::max_attempts;
	}
	if (count_attempts (ip) >= config.max_attempts_per_ip)
	{
		return result::max_attempts_per_ip;
	}
	return result::accepted;
}

void nano::transport::tcp_listener_index::insert (connection_type type, nano::tcp_endpoint const & endpoint, std::shared_ptr<tcp_socket> socket, std::shared_ptr<tcp_server> server)
{
	auto const address = endpoint.address ();
	connections.push_back ({ type, endpoint, nano::transport::ipv4_address_or_ipv6_subnet (address), nano::transport::map_address_to_subnetwork (address), std::move (socket), std::move (server) });
}

void nano::transport::tcp_listener_index::insert_attempt (nano::tcp_endpoint const & endpoint, std::shared_ptr<nano::async::task> task, std::chrono::steady_clock::time_point now)
{
	attempts.push_back ({ endpoint, nano::transport::ipv4_address_or_ipv6_subnet (endpoint.address ()), std::move (task), now });
}

auto nano::transport::tcp_listener_index::erase_if (std::function<bool (connection const &)> const & predicate, std::chrono::steady_clock::time_point now) -> std::deque<connection>
{
	std::deque<connection> erased;
	for (auto it = connections.begin (); it != connections.end ();)
	{
		if (predicate (*it))
		{
			if (config.reconnect_cooldown > 0s)
			{
				cooldowns.insert (cooldown{ it->address, it->subnetwork, now + config.reconnect_cooldown });
			}
			erased.push_back (*it);
			it = connections.erase (it);
		}
		else
		{
			++it;
		}
	}

	return erased;
}

auto nano::transport::tcp_listener_index::erase_attempts_if (std::function<bool (attempt const &)> const & predicate) -> std::deque<attempt>
{
	std::deque<attempt> erased;
	for (auto it = attempts.begin (); it != attempts.end ();)
	{
		if (predicate (*it))
		{
			erased.push_back (*it);
			it = attempts.erase (it);
		}
		else
		{
			++it;
		}
	}

	return erased;
}

void nano::transport::tcp_listener_index::update (std::chrono::steady_clock::time_point now)
{
	auto & by_expiry = cooldowns.get<tag_expiry> ();
	by_expiry.erase (by_expiry.begin (), by_expiry.upper_bound (now));
}

auto nano::transport::tcp_listener_index::clear () -> contents
{
	contents result;
	result.connections.assign (connections.begin (), connections.end ());
	result.attempts.assign (attempts.begin (), attempts.end ());
	connections.clear ();
	attempts.clear ();
	cooldowns.clear ();
	return result;
}

auto nano::transport::tcp_listener_index::list_connections () const -> std::deque<connection>
{
	return { connections.begin (), connections.end () };
}

auto nano::transport::tcp_listener_index::list_attempts () const -> std::deque<attempt>
{
	return { attempts.begin (), attempts.end () };
}

size_t nano::transport::tcp_listener_index::size () const
{
	return connections.size ();
}

size_t nano::transport::tcp_listener_index::size (connection_type type) const
{
	return connections.get<tag_type> ().count (type);
}

size_t nano::transport::tcp_listener_index::attempt_count () const
{
	return attempts.size ();
}

size_t nano::transport::tcp_listener_index::cooldown_count () const
{
	return cooldowns.size ();
}

size_t nano::transport::tcp_listener_index::count_per_ip (asio::ip::address const & ip) const
{
	auto const address = nano::transport::ipv4_address_or_ipv6_subnet (ip);
	auto const connected = connections.get<tag_address> ().count (address);
	auto const cooling = cooldowns.get<tag_address> ().count (address);
	return connected + cooling;
}

size_t nano::transport::tcp_listener_index::count_per_subnetwork (asio::ip::address const & ip) const
{
	auto const subnetwork = nano::transport::map_address_to_subnetwork (ip);
	auto const connected = connections.get<tag_subnetwork> ().count (subnetwork);
	auto const cooling = cooldowns.get<tag_subnetwork> ().count (subnetwork);
	return connected + cooling;
}

size_t nano::transport::tcp_listener_index::count_attempts (asio::ip::address const & ip) const
{
	return attempts.get<tag_address> ().count (nano::transport::ipv4_address_or_ipv6_subnet (ip));
}

nano::container_info nano::transport::tcp_listener_index::container_info () const
{
	nano::container_info info;
	info.put ("connections", connections.size ());
	info.put ("attempts", attempts.size ());
	info.put ("cooldowns", cooldowns.size ());
	return info;
}
