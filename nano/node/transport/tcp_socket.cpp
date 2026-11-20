#include <nano/boost/asio/bind_executor.hpp>
#include <nano/boost/asio/read.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/network_formatting.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_config.hpp>
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

nano::transport::tcp_socket::tcp_socket (nano::node & node_a, asio::ip::tcp::socket raw_socket_a, nano::endpoint const & remote_endpoint_a, nano::transport::socket_endpoint endpoint_type_a) :
	node{ node_a },
	strand{ node_a.io_ctx.get_executor () },
	task{ strand },
	raw_socket{ std::move (raw_socket_a) },
	remote_endpoint{ remote_endpoint_a },
	endpoint_type{ endpoint_type_a },
	connected{ true },
	time_connected{ std::chrono::steady_clock::now () }
{
	// Best effort, this side's endpoint does not depend on the peer still being there
	boost::system::error_code ec_ignored;
	local_endpoint = raw_socket.local_endpoint (ec_ignored);

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
		task.cancel ();
		task.join ();
	}
}

auto nano::transport::tcp_socket::ongoing_checkup () -> asio::awaitable<void>
{
	debug_assert (strand.running_in_this_thread ());

	auto const ended = co_await nano::async::settle (run_checkup ());
	debug_assert (strand.running_in_this_thread ());

	// A bug in one connection must not take the node down, so it only closes that connection
	if (ended.unexpected)
	{
		node.logger.error (nano::log::type::tcp_socket, "Checkup stopped by an unexpected exception: {} ({})", *ended.unexpected, remote_endpoint);
		debug_assert (false, "unexpected exception");
	}

	// Close the socket if checkup task is canceled for any reason
	close_impl ();
}

auto nano::transport::tcp_socket::run_checkup () -> asio::awaitable<void>
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
			std::chrono::seconds sleep_duration = node.config.tcp->checkup_interval;
			co_await nano::async::sleep_for (sleep_duration);
			timestamp += sleep_duration.count ();
		}
	}
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

		std::chrono::seconds const io_threshold = node.config.tcp->io_timeout;
		std::chrono::seconds const silence_threshold = node.config.tcp->silent_timeout;

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
		auto const cutoff = now - node.config.tcp->connect_timeout;

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

// TODO: This is only used in tests, remove it, this creates untracked socket
auto nano::transport::tcp_socket::co_connect_impl (nano::endpoint endpoint) -> asio::awaitable<nano::io_result<void>>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (endpoint_type == socket_endpoint::client);
	debug_assert (!raw_socket.is_open ());
	debug_assert (connect_in_progress.exchange (true) == false);

	auto const result = co_await raw_socket.async_connect (endpoint, nano::async::use_result);
	debug_assert (connect_in_progress.exchange (false) == true);

	if (!result)
	{
		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_connect_error);
		node.stats.inc (nano::stat::type::tcp_socket, nano::stat::detail::connect_error);
		node.stats.inc (nano::stat::type::tcp_socket_connect_ec, nano::to_stat_detail (result.error ()));
		node.logger.debug (nano::log::type::tcp_socket, "Failed to connect to: {} ({})", endpoint, result.error ());

		error = true;
		close_impl ();

		co_return result;
	}

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

	co_return result;
}

void nano::transport::tcp_socket::async_connect (nano::endpoint endpoint, std::function<void (boost::system::error_code const &)> callback)
{
	spawn_for_callback (co_connect_impl (endpoint), std::move (callback));
}

void nano::transport::tcp_socket::spawn_for_callback (asio::awaitable<nano::io_result<void>> operation, std::function<void (boost::system::error_code const &)> callback)
{
	debug_assert (callback);
	// The callback runs on the strand, and settle lets no exception reach it
	asio::co_spawn (strand, nano::async::settle (std::move (operation)), [callback, /* lifetime guard */ this_s = shared_from_this ()] (std::exception_ptr const &, nano::async::settled const & ended) {
		// A bug in one connection must not take the node down, so it only closes that connection
		if (ended.unexpected)
		{
			this_s->node.logger.error (nano::log::type::tcp_socket, "Operation stopped by an unexpected exception: {} ({})", *ended.unexpected, this_s->remote_endpoint);
			debug_assert (false, "unexpected exception");
			this_s->close_impl ();
		}
		callback (ended.ec); // Empty on success
	});
}

boost::system::error_code nano::transport::tcp_socket::blocking_connect (nano::endpoint endpoint)
{
	auto fut = asio::co_spawn (strand, co_connect_impl (endpoint), asio::use_future);
	fut.wait (); // Blocking call
	auto const result = fut.get ();
	return result.error (); // Empty on success
}

auto nano::transport::tcp_socket::co_read (nano::shared_buffer buffer, size_t target_size) -> asio::awaitable<nano::io_result<void>>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (read_in_progress.exchange (true) == false);
	release_assert (target_size <= buffer->size (), "read buffer size mismatch");

	read_timestamp = timestamp;
	auto const size_read = co_await asio::async_read (raw_socket, asio::buffer (buffer->data (), target_size), nano::async::use_result);
	read_timestamp = 0;
	debug_assert (read_in_progress.exchange (false) == true);

	if (!size_read)
	{
		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_read_error);
		node.stats.inc (nano::stat::type::tcp_socket_read_ec, nano::to_stat_detail (size_read.error ()));
		node.logger.debug (nano::log::type::tcp_socket, "Error reading from: {} ({})", remote_endpoint, size_read.error ());

		error = true;
		close_impl ();

		co_return size_read.error ();
	}

	// Callers take the whole buffer as read, so a completed read must have filled it
	release_assert (*size_read == target_size, "read completed without filling the buffer");

	last_receive = timestamp;
	node.stats.add (nano::stat::type::traffic_tcp, nano::stat::detail::all, nano::stat::dir::in, *size_read);

	co_return nano::io_success ();
}

void nano::transport::tcp_socket::async_read (nano::shared_buffer buffer, size_t size, std::function<void (boost::system::error_code const &)> callback)
{
	spawn_for_callback (co_read (buffer, size), std::move (callback));
}

boost::system::error_code nano::transport::tcp_socket::blocking_read (nano::shared_buffer buffer, size_t size)
{
	auto fut = asio::co_spawn (strand, co_read (buffer, size), asio::use_future);
	fut.wait (); // Blocking call
	auto const result = fut.get ();
	return result.error (); // Empty on success
}

auto nano::transport::tcp_socket::co_write (nano::shared_buffer buffer, size_t target_size) -> asio::awaitable<nano::io_result<void>>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (write_in_progress.exchange (true) == false);
	release_assert (target_size <= buffer->size (), "write buffer size mismatch");

	write_timestamp = timestamp;
	auto const size_written = co_await asio::async_write (raw_socket, asio::buffer (buffer->data (), target_size), nano::async::use_result);
	write_timestamp = 0;
	debug_assert (write_in_progress.exchange (false) == true);

	if (!size_written)
	{
		node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_write_error);
		node.stats.inc (nano::stat::type::tcp_socket_write_ec, nano::to_stat_detail (size_written.error ()));
		node.logger.debug (nano::log::type::tcp_socket, "Error writing to: {} ({})", remote_endpoint, size_written.error ());

		error = true;
		close_impl ();

		co_return size_written.error ();
	}

	debug_assert (*size_written == target_size);

	last_send = timestamp;
	node.stats.add (nano::stat::type::traffic_tcp, nano::stat::detail::all, nano::stat::dir::out, *size_written);

	co_return nano::io_success ();
}

void nano::transport::tcp_socket::async_write (nano::shared_buffer buffer, std::function<void (boost::system::error_code const &)> callback)
{
	spawn_for_callback (co_write (buffer, buffer->size ()), std::move (callback));
}

boost::system::error_code nano::transport::tcp_socket::blocking_write (nano::shared_buffer buffer, size_t size)
{
	auto fut = asio::co_spawn (strand, co_write (buffer, size), asio::use_future);
	fut.wait (); // Blocking call
	auto const result = fut.get ();
	return result.error (); // Empty on success
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

nano::async::strand & nano::transport::tcp_socket::get_strand ()
{
	return strand;
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
