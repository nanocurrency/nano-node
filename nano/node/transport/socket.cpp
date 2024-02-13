#include <nano/boost/asio/bind_executor.hpp>
#include <nano/boost/asio/read.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/socket.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/format.hpp>

#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

#include <magic_enum.hpp>

#include <sanitizer/asan_interface.h>

/*
 * socket
 */

nano::transport::socket::socket (nano::node & node_a, endpoint_type_t endpoint_type_a, std::size_t max_queue_size_a) :
	send_queue{ max_queue_size_a },
	strand{ node_a.io_ctx.get_executor () },
	tcp_socket{ node_a.io_ctx },
	node{ node_a },
	endpoint_type_m{ endpoint_type_a },
	timeout{ std::numeric_limits<uint64_t>::max () },
	last_completion_time_or_init{ nano::seconds_since_epoch () },
	last_receive_time_or_init{ nano::seconds_since_epoch () },
	default_timeout{ node_a.config.tcp_io_timeout },
	silent_connection_tolerance_time{ node_a.network_params.network.silent_connection_tolerance_time },
	max_queue_size{ max_queue_size_a }
{
	__asan_poison_memory_region (poison1.data (), poison1.size ());
	__asan_poison_memory_region (poison2.data (), poison2.size ());
}

nano::transport::socket::~socket ()
{
	close_internal ();
}

void nano::transport::socket::start ()
{
	ongoing_checkup ();
}

void nano::transport::socket::async_connect (nano::tcp_endpoint const & endpoint_a, std::function<void (boost::system::error_code const &)> callback_a)
{
	debug_assert (callback_a);
	debug_assert (endpoint_type () == endpoint_type_t::client);

	start ();
	auto this_l (shared_from_this ());
	set_default_timeout ();

	this_l->tcp_socket.async_connect (endpoint_a,
	boost::asio::bind_executor (this_l->strand,
	[this_l, callback = std::move (callback_a), endpoint_a] (boost::system::error_code const & ec) {
		this_l->remote = endpoint_a;
		if (ec)
		{
			this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_connect_error, nano::stat::dir::in);
			this_l->close ();
		}
		else
		{
			this_l->set_last_completion ();
			{
				// Best effort attempt to get endpoint address
				boost::system::error_code ec;
				this_l->local = this_l->tcp_socket.local_endpoint (ec);
			}
			this_l->node.observers.socket_connected.notify (*this_l);
		}
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

void nano::transport::socket::async_write (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a, nano::transport::traffic_type traffic_type)
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

	bool queued = send_queue.insert (buffer_a, callback_a, traffic_type);
	if (!queued)
	{
		if (callback_a)
		{
			node.background ([callback = std::move (callback_a)] () {
				callback (boost::system::errc::make_error_code (boost::system::errc::not_supported), 0);
			});
		}
		return;
	}

	boost::asio::post (strand, boost::asio::bind_executor (strand, [this_s = shared_from_this (), buffer_a, callback_a, traffic_type] () {
		if (!this_s->write_in_progress)
		{
			this_s->write_queued_messages ();
		}
	}));
}

// Must be called from strand
void nano::transport::socket::write_queued_messages ()
{
	if (closed)
	{
		return;
	}

	auto next = send_queue.pop ();
	if (!next)
	{
		return;
	}

	set_default_timeout ();

	write_in_progress = true;
	nano::async_write (tcp_socket, next->buffer,
	boost::asio::bind_executor (strand, [this_s = shared_from_this (), next /* `next` object keeps buffer in scope */] (boost::system::error_code ec, std::size_t size) {
		this_s->write_in_progress = false;

		if (ec)
		{
			this_s->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_write_error, nano::stat::dir::in);
			this_s->close ();
		}
		else
		{
			this_s->node.stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::out, size);
			this_s->set_last_completion ();
		}

		if (next->callback)
		{
			next->callback (ec, size);
		}

		if (!ec)
		{
			this_s->write_queued_messages ();
		}
	}));
}

bool nano::transport::socket::max (nano::transport::traffic_type traffic_type) const
{
	return send_queue.size (traffic_type) >= max_queue_size;
}

bool nano::transport::socket::full (nano::transport::traffic_type traffic_type) const
{
	return send_queue.size (traffic_type) >= 2 * max_queue_size;
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

void nano::transport::socket::ongoing_checkup ()
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

			nano::seconds_t now = nano::seconds_since_epoch ();
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
				this_l->node.stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, this_l->endpoint_type () == endpoint_type_t::server ? nano::stat::dir::in : nano::stat::dir::out);

				condition_to_disconnect = true;
			}

			if (condition_to_disconnect)
			{
				this_l->node.logger.debug (nano::log::type::tcp_server, "Closing socket due to timeout ({})", nano::util::to_str (this_l->remote));

				this_l->timed_out = true;
				this_l->close ();
			}
			else if (!this_l->closed)
			{
				this_l->ongoing_checkup ();
			}
		}
	});
}

void nano::transport::socket::read_impl (std::shared_ptr<std::vector<uint8_t>> const & data_a, std::size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a)
{
	// Increase timeout to receive TCP header (idle server socket)
	auto const prev_timeout = get_default_timeout_value ();
	set_default_timeout_value (node.network_params.network.idle_timeout);
	async_read (data_a, size_a, [callback_l = std::move (callback_a), prev_timeout, this_l = shared_from_this ()] (boost::system::error_code const & ec_a, std::size_t size_a) {
		this_l->set_default_timeout_value (prev_timeout);
		callback_l (ec_a, size_a);
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
	if (closed.exchange (true))
	{
		return;
	}

	send_queue.clear ();

	default_timeout = std::chrono::seconds (0);
	boost::system::error_code ec;

	// Ignore error code for shutdown as it is best-effort
	tcp_socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both, ec);
	tcp_socket.close (ec);

	if (ec)
	{
		node.stats.inc (nano::stat::type::socket, nano::stat::detail::error_socket_close);
		node.logger.error (nano::log::type::socket, "Failed to close socket gracefully: {} ({})", ec.message (), nano::util::to_str (remote));
	}
}

nano::tcp_endpoint nano::transport::socket::remote_endpoint () const
{
	// Using cached value to avoid calling tcp_socket.remote_endpoint() which may be invalid (throw) after closing the socket
	return remote;
}

nano::tcp_endpoint nano::transport::socket::local_endpoint () const
{
	// Using cached value to avoid calling tcp_socket.local_endpoint() which may be invalid (throw) after closing the socket
	return local;
}

void nano::transport::socket::operator() (nano::object_stream & obs) const
{
	obs.write ("remote_endpoint", remote_endpoint ());
	obs.write ("local_endpoint", local_endpoint ());
	obs.write ("type", type_m);
	obs.write ("endpoint_type", endpoint_type_m);
}

/*
 * write_queue
 */

nano::transport::socket::write_queue::write_queue (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

bool nano::transport::socket::write_queue::insert (const buffer_t & buffer, callback_t callback, nano::transport::traffic_type traffic_type)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	if (queues[traffic_type].size () < 2 * max_size)
	{
		queues[traffic_type].push (entry{ buffer, callback });
		return true; // Queued
	}
	return false; // Not queued
}

std::optional<nano::transport::socket::write_queue::entry> nano::transport::socket::write_queue::pop ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto try_pop = [this] (nano::transport::traffic_type type) -> std::optional<entry> {
		auto & que = queues[type];
		if (!que.empty ())
		{
			auto item = que.front ();
			que.pop ();
			return item;
		}
		return std::nullopt;
	};

	// TODO: This is a very basic prioritization, implement something more advanced and configurable
	if (auto item = try_pop (nano::transport::traffic_type::generic))
	{
		return item;
	}
	if (auto item = try_pop (nano::transport::traffic_type::bootstrap))
	{
		return item;
	}

	return std::nullopt;
}

void nano::transport::socket::write_queue::clear ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	queues.clear ();
}

std::size_t nano::transport::socket::write_queue::size (nano::transport::traffic_type traffic_type) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	if (auto it = queues.find (traffic_type); it != queues.end ())
	{
		return it->second.size ();
	}
	return 0;
}

bool nano::transport::socket::write_queue::empty () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return std::all_of (queues.begin (), queues.end (), [] (auto const & que) {
		return que.second.empty ();
	});
}

boost::asio::ip::network_v6 nano::transport::socket_functions::get_ipv6_subnet_address (boost::asio::ip::address_v6 const & ip_address, std::size_t network_prefix)
{
	return boost::asio::ip::make_network_v6 (ip_address, static_cast<unsigned short> (network_prefix));
}

boost::asio::ip::address nano::transport::socket_functions::first_ipv6_subnet_address (boost::asio::ip::address_v6 const & ip_address, std::size_t network_prefix)
{
	auto range = get_ipv6_subnet_address (ip_address, network_prefix).hosts ();
	debug_assert (!range.empty ());
	return *(range.begin ());
}

boost::asio::ip::address nano::transport::socket_functions::last_ipv6_subnet_address (boost::asio::ip::address_v6 const & ip_address, std::size_t network_prefix)
{
	auto range = get_ipv6_subnet_address (ip_address, network_prefix).hosts ();
	debug_assert (!range.empty ());
	return *(--range.end ());
}

std::size_t nano::transport::socket_functions::count_subnetwork_connections (
nano::transport::address_socket_mmap const & per_address_connections,
boost::asio::ip::address_v6 const & remote_address,
std::size_t network_prefix)
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

std::string_view nano::transport::to_string (nano::transport::socket::type_t type)
{
	return magic_enum::enum_name (type);
}
