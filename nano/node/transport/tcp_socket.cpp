#include <nano/boost/asio/bind_executor.hpp>
#include <nano/boost/asio/read.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/node/transport/transport.hpp>

#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

nano::transport::tcp_socket::tcp_socket (nano::node & node_a, nano::transport::socket_endpoint endpoint_type_a) :
	node{ node_a },
	strand{ node_a.io_ctx.get_executor () },
	task{ strand },
	raw_socket{ node_a.io_ctx },
	endpoint_type{ endpoint_type_a }
{
	start ();
}

nano::transport::tcp_socket::tcp_socket (nano::node & node_a, asio::ip::tcp::socket raw_socket_a, nano::transport::socket_endpoint endpoint_type_a) :
	node{ node_a },
	strand{ node_a.io_ctx.get_executor () },
	task{ strand },
	raw_socket{ std::move (raw_socket_a) },
	local_endpoint{ raw_socket.local_endpoint () },
	remote_endpoint{ raw_socket.remote_endpoint () },
	endpoint_type{ endpoint_type_a },
	connected{ true },
	time_connected{ std::chrono::steady_clock::now () }
{
	start ();
}

nano::transport::tcp_socket::~tcp_socket ()
{
	close ();
}

void nano::transport::tcp_socket::close ()
{
	stop ();

	if (closed) // Avoid closing the socket multiple times
	{
		return;
	}

	// Node context must be running to gracefully stop async tasks
	debug_assert (!node.io_ctx.stopped ());
	// Ensure that we are not trying to await the task while running on the same thread / io_context
	debug_assert (!node.io_ctx.get_executor ().running_in_this_thread ());

	// Dispatch close raw socket to the strand, wait synchronously for the operation to complete
	auto fut = asio::dispatch (strand, asio::use_future ([this] () {
		close_impl ();
	}));
	fut.wait (); // Blocking call
}

void nano::transport::tcp_socket::close_async ()
{
	// Node context must be running to gracefully stop async tasks
	debug_assert (!node.io_ctx.stopped ());

	asio::dispatch (strand, [this, /* lifetime guard */ this_s = shared_from_this ()] () {
		close_impl ();
	});
}

void nano::transport::tcp_socket::close_impl ()
{
	debug_assert (strand.running_in_this_thread ());

	if (closed.exchange (true)) // Avoid closing the socket multiple times
	{
		return;
	}

	boost::system::error_code ec;
	raw_socket.shutdown (asio::ip::tcp::socket::shutdown_both, ec); // Best effort, ignore errors
	raw_socket.close (ec); // Best effort, ignore errors
	if (!ec)
	{
		node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::close);
		node.logger.debug (nano::log::type::tcp_socket, "Closed socket: {}", remote_endpoint);
	}
	else
	{
		node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::close_error);
		node.logger.debug (nano::log::type::tcp_socket, "Closed socket, ungracefully: {} ({})", remote_endpoint, ec.message ());
	}
}

void nano::transport::tcp_socket::start ()
{
	release_assert (!task.joinable ());
	task = nano::async::task (strand, ongoing_checkup ());
}

void nano::transport::tcp_socket::stop ()
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

auto nano::transport::tcp_socket::ongoing_checkup () -> asio::awaitable<void>
{
	debug_assert (strand.running_in_this_thread ());
	try
	{
		while (!co_await nano::async::cancelled () && alive ())
		{
			bool healthy = checkup ();
			if (!healthy)
			{
				node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::unhealthy);
				node.logger.debug (nano::log::type::tcp_socket, "Unhealthy socket detected: {} (timed out: {})",
				remote_endpoint,
				timed_out.load ());

				close_impl ();

				break; // Stop the checkup task
			}
			else
			{
				std::chrono::seconds sleep_duration = node.config.tcp.checkup_interval;
				co_await nano::async::sleep_for (sleep_duration);
				timestamp += sleep_duration.count ();
			}
		}
	}
	catch (boost::system::system_error const & ex)
	{
		// Operation aborted is expected when cancelling the acceptor
		debug_assert (ex.code () == asio::error::operation_aborted);
	}
	debug_assert (strand.running_in_this_thread ());

	// Close the socket if checkup task is canceled for any reason
	close_impl ();
}

bool nano::transport::tcp_socket::checkup ()
{
	debug_assert (strand.running_in_this_thread ());

	if (connected)
	{
		if (!raw_socket.is_open ())
		{
			node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::already_closed);
			return false; // Bad
		}

		debug_assert (timestamp >= read_timestamp);
		debug_assert (timestamp >= write_timestamp);
		debug_assert (timestamp >= last_receive);
		debug_assert (timestamp >= last_send);

		std::chrono::seconds const io_threshold = node.config.tcp.io_timeout;
		std::chrono::seconds const silence_threshold = node.config.tcp.silent_timeout;

		// Timeout threshold of 0 indicates no timeout
		if (io_threshold.count () > 0)
		{
			if (read_timestamp > 0 && timestamp - read_timestamp > io_threshold.count ())
			{
				node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::timeout);
				node.stats.inc (nano::stat::type::tcp_socket_timeout, nano::stat::detail::timeout_receive);
				node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::in);

				timed_out = true;
				return false; // Bad
			}
			if (write_timestamp > 0 && timestamp - write_timestamp > io_threshold.count ())
			{
				node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::timeout);
				node.stats.inc (nano::stat::type::tcp_socket_timeout, nano::stat::detail::timeout_send);
				node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out);

				timed_out = true;
				return false; // Bad
			}
		}
		if (silence_threshold.count () > 0)
		{
			if ((timestamp - last_receive) > silence_threshold.count () || (timestamp - last_send) > silence_threshold.count ())
			{
				node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::timeout);
				node.stats.inc (nano::stat::type::tcp_socket_timeout, nano::stat::detail::timeout_silence);
				node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_silent_connection_drop, nano::stat::dir::in);

				timed_out = true;
				return false; // Bad
			}
		}
	}
	else // Not connected yet
	{
		auto const now = std::chrono::steady_clock::now ();
		auto const cutoff = now - node.config.tcp.connect_timeout;

		if (time_created < cutoff)
		{
			node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::timeout);
			node.stats.inc (nano::stat::type::tcp_socket_timeout, nano::stat::detail::timeout_connect);

			timed_out = true;
			return false; // Bad
		}
	}

	return true; // Healthy
}

auto nano::transport::tcp_socket::co_connect (nano::endpoint endpoint) -> asio::awaitable<std::tuple<boost::system::error_code>>
{
	// Dispatch operation to the strand
	// TODO: This additional dispatch should not be necessary, but it is done during transition to coroutine based code
	co_return co_await asio::co_spawn (strand, co_connect_impl (endpoint), asio::use_awaitable);
}

// TODO: This is only used in tests, remove it, this creates untracked socket
auto nano::transport::tcp_socket::co_connect_impl (nano::endpoint endpoint) -> asio::awaitable<std::tuple<boost::system::error_code>>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (endpoint_type == socket_endpoint::client);
	debug_assert (!raw_socket.is_open ());
	debug_assert (connect_in_progress.exchange (true) == false);

	auto result = co_await raw_socket.async_connect (endpoint, asio::as_tuple (asio::use_awaitable));
	auto const & [ec] = result;
	if (!ec)
	{
		// Best effort to get the endpoints
		boost::system::error_code ec_ignored;
		local_endpoint = raw_socket.local_endpoint (ec_ignored);
		remote_endpoint = raw_socket.remote_endpoint (ec_ignored);

		connected = true; // Mark as connected
		time_connected = std::chrono::steady_clock::now ();

		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_connect_success);
		node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::connect_success);
		node.logger.debug (nano::log::type::tcp_socket, "Successfully connected to: {} from local: {}",
		remote_endpoint, local_endpoint);
	}
	else
	{
		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_connect_error);
		node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::connect_error);
		node.logger.debug (nano::log::type::tcp_socket, "Failed to connect to: {} ({})",
		endpoint, local_endpoint, ec);

		error = true;
		close_impl ();
	}
	debug_assert (connect_in_progress.exchange (false) == true);
	co_return result;
}

void nano::transport::tcp_socket::async_connect (nano::endpoint endpoint, std::function<void (boost::system::error_code const &)> callback)
{
	debug_assert (callback);
	asio::co_spawn (strand, co_connect_impl (endpoint), [callback, /* lifetime guard */ this_s = shared_from_this ()] (std::exception_ptr const & ex, auto const & result) {
		release_assert (!ex);
		auto const & [ec] = result;
		callback (ec);
	});
}

boost::system::error_code nano::transport::tcp_socket::blocking_connect (nano::endpoint endpoint)
{
	auto fut = asio::co_spawn (strand, co_connect_impl (endpoint), asio::use_future);
	fut.wait (); // Blocking call
	auto result = fut.get ();
	auto const & [ec] = result;
	return ec;
}

auto nano::transport::tcp_socket::co_read (nano::shared_buffer buffer, size_t target_size) -> asio::awaitable<std::tuple<boost::system::error_code, size_t>>
{
	// Dispatch operation to the strand
	// TODO: This additional dispatch should not be necessary, but it is done during transition to coroutine based code
	co_return co_await asio::co_spawn (strand, co_read_impl (buffer, target_size), asio::use_awaitable);
}

auto nano::transport::tcp_socket::co_read_impl (nano::shared_buffer buffer, size_t target_size) -> asio::awaitable<std::tuple<boost::system::error_code, size_t>>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (read_in_progress.exchange (true) == false);
	release_assert (target_size <= buffer->size (), "read buffer size mismatch");

	read_timestamp = timestamp;
	auto result = co_await asio::async_read (raw_socket, asio::buffer (buffer->data (), target_size), asio::as_tuple (asio::use_awaitable));
	auto const & [ec, size_read] = result;
	read_timestamp = 0;
	if (!ec)
	{
		last_receive = timestamp;
		node.stats.add (nano::stat::type::traffic_tcp, nano::stat::detail::all, nano::stat::dir::in, size_read);
	}
	else
	{
		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_read_error);
		node.logger.debug (nano::log::type::tcp_socket, "Error reading from: {} ({})", remote_endpoint, ec);

		error = true;
		close_impl ();
	}
	debug_assert (read_in_progress.exchange (false) == true);
	co_return result;
}

void nano::transport::tcp_socket::async_read (nano::shared_buffer buffer, size_t size, std::function<void (boost::system::error_code const &, size_t)> callback)
{
	debug_assert (callback);
	asio::co_spawn (strand, co_read_impl (buffer, size), [callback, /* lifetime guard */ this_s = shared_from_this ()] (std::exception_ptr const & ex, auto const & result) {
		release_assert (!ex);
		auto const & [ec, size] = result;
		callback (ec, size);
	});
}

auto nano::transport::tcp_socket::blocking_read (nano::shared_buffer buffer, size_t size) -> std::tuple<boost::system::error_code, size_t>
{
	auto fut = asio::co_spawn (strand, co_read_impl (buffer, size), asio::use_future);
	fut.wait (); // Blocking call
	auto result = fut.get ();
	return result;
}

auto nano::transport::tcp_socket::co_write (nano::shared_buffer buffer, size_t target_size) -> asio::awaitable<std::tuple<boost::system::error_code, size_t>>
{
	// Dispatch operation to the strand
	// TODO: This additional dispatch should not be necessary, but it is done during transition to coroutine based code
	co_return co_await asio::co_spawn (strand, co_write_impl (buffer, target_size), asio::use_awaitable);
}

auto nano::transport::tcp_socket::co_write_impl (nano::shared_buffer buffer, size_t target_size) -> asio::awaitable<std::tuple<boost::system::error_code, size_t>>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (write_in_progress.exchange (true) == false);
	release_assert (target_size <= buffer->size (), "write buffer size mismatch");

	write_timestamp = timestamp;
	auto result = co_await asio::async_write (raw_socket, asio::buffer (buffer->data (), target_size), asio::as_tuple (asio::use_awaitable));
	auto const & [ec, size_written] = result;
	write_timestamp = 0;
	if (!ec)
	{
		last_send = timestamp;
		node.stats.add (nano::stat::type::traffic_tcp, nano::stat::detail::all, nano::stat::dir::out, size_written);
	}
	else
	{
		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_write_error);
		node.logger.debug (nano::log::type::tcp_socket, "Error writing to: {} ({})", remote_endpoint, ec);

		error = true;
		close_impl ();
	}
	debug_assert (write_in_progress.exchange (false) == true);
	co_return result;
}

void nano::transport::tcp_socket::async_write (nano::shared_buffer buffer, std::function<void (boost::system::error_code const &, size_t)> callback)
{
	debug_assert (callback);
	asio::co_spawn (strand, co_write_impl (buffer, buffer->size ()), [callback, /* lifetime guard */ this_s = shared_from_this ()] (std::exception_ptr const & ex, auto const & result) {
		release_assert (!ex);
		auto const & [ec, size] = result;
		callback (ec, size);
	});
}

auto nano::transport::tcp_socket::blocking_write (nano::shared_buffer buffer, size_t size) -> std::tuple<boost::system::error_code, size_t>
{
	auto fut = asio::co_spawn (strand, co_write_impl (buffer, size), asio::use_future);
	fut.wait (); // Blocking call
	auto result = fut.get ();
	return result;
}

nano::endpoint nano::transport::tcp_socket::get_remote_endpoint () const
{
	// Using cached value to avoid calling tcp_socket.remote_endpoint() which may be invalid (throw) after closing the socket
	return remote_endpoint;
}

nano::endpoint nano::transport::tcp_socket::get_local_endpoint () const
{
	// Using cached value to avoid calling tcp_socket.local_endpoint() which may be invalid (throw) after closing the socket
	return local_endpoint;
}

nano::transport::socket_endpoint nano::transport::tcp_socket::get_endpoint_type () const
{
	return endpoint_type;
}

bool nano::transport::tcp_socket::alive () const
{
	return !closed;
}

bool nano::transport::tcp_socket::has_connected () const
{
	return connected;
}

bool nano::transport::tcp_socket::has_timed_out () const
{
	return timed_out;
}

std::chrono::steady_clock::time_point nano::transport::tcp_socket::get_time_created () const
{
	return time_created;
}

std::chrono::steady_clock::time_point nano::transport::tcp_socket::get_time_connected () const
{
	return time_connected;
}

void nano::transport::tcp_socket::operator() (nano::object_stream & obs) const
{
	obs.write ("remote_endpoint", remote_endpoint);
	obs.write ("local_endpoint", local_endpoint);
	obs.write ("type", type_m.load ());
	obs.write ("endpoint_type", endpoint_type);
}

/*
 *
 */

std::string_view nano::transport::to_string (socket_type type)
{
	return nano::enum_to_string (type);
}

std::string_view nano::transport::to_string (socket_endpoint type)
{
	return nano::enum_to_string (type);
}
