#include <nano/lib/logging.hpp>
#include <nano/lib/network_formatting.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/stacktrace.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_channel.hpp>
#include <nano/node/transport/tcp_config.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/network_params.hpp>

/*
 * tcp_channel
 */

nano::transport::tcp_channel::tcp_channel (nano::node & node_a, std::shared_ptr<nano::transport::tcp_socket> socket_a, nano::transport::peer_info const & peer) :
	channel{ node_a, peer },
	socket{ socket_a },
	strand{ socket_a->get_strand () },
	sending_task{ strand },
	queue{ [] (nano::transport::traffic_type) { return full_queue_size; }, nano::transport::traffic_priority }
{
	remote_endpoint = socket_a->get_remote_endpoint ();
	local_endpoint = socket_a->get_local_endpoint ();
	start ();
}

nano::transport::tcp_channel::~tcp_channel ()
{
	close ();
}

void nano::transport::tcp_channel::close ()
{
	stop ();
	socket->close ();
}

void nano::transport::tcp_channel::close_async ()
{
	socket->close_async ();
}

void nano::transport::tcp_channel::start ()
{
	sending_task = nano::async::task (strand, [this] (nano::async::condition & condition) {
		return start_sending (condition); // This is not a coroutine, but a corotuine factory
	});
}

asio::awaitable<void> nano::transport::tcp_channel::start_sending (nano::async::condition & condition)
{
	debug_assert (strand.running_in_this_thread ());

	auto const ended = co_await nano::async::settle (run_sending (condition));
	debug_assert (strand.running_in_this_thread ());

	if (ended.ec)
	{
		node.logger.debug (nano::log::type::channel, "Sending stopped: {} ({})", ended.ec, get_remote_endpoint ());
	}

	// A bug in one connection must not take the node down, so it only closes that connection
	if (ended.unexpected)
	{
		node.logger.error (nano::log::type::channel, "Sending stopped by an unexpected exception: {} ({})", *ended.unexpected, get_remote_endpoint ());
		debug_assert (false, "unexpected exception");
	}

	// Ensure socket gets closed if task is stopped
	close_async ();
}

void nano::transport::tcp_channel::stop ()
{
	if (sending_task.running ())
	{
		sending_task.cancel ();
		sending_task.join ();
	}
}

bool nano::transport::tcp_channel::max_impl (nano::transport::traffic_type traffic_type)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return queue.size (traffic_type) >= max_queue_size;
}

bool nano::transport::tcp_channel::send_buffer_impl (nano::shared_const_buffer const & buffer, nano::transport::traffic_type type, nano::transport::send_callback callback)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	// A full queue is checked first, so a dropped send builds no entry
	if (!queue.full (type) && queue.push ({ buffer, std::move (callback) }, type))
	{
		lock.unlock ();

		node.stats.inc (nano::stat::type::tcp_channel, nano::stat::detail::queued, nano::stat::dir::out);
		node.stats.inc (nano::stat::type::tcp_channel_queued, to_stat_detail (type), nano::stat::dir::out);

		sending_task.notify (); // Wake up the sending task

		return true;
	}
	else
	{
		lock.unlock ();

		node.stats.inc (nano::stat::type::tcp_channel, nano::stat::detail::drop, nano::stat::dir::out);
		node.stats.inc (nano::stat::type::tcp_channel_drop, to_stat_detail (type), nano::stat::dir::out);
	}
	return false;
}

auto nano::transport::tcp_channel::run_sending (nano::async::condition & condition) -> asio::awaitable<nano::io_result<void>>
{
	while (!co_await nano::async::cancelled ())
	{
		debug_assert (strand.running_in_this_thread ());

		auto next_batch = [this] () {
			nano::lock_guard<nano::mutex> lock{ mutex };
			return queue.next_batch (nano::transport::traffic_queue::batch_capacity);
		};

		if (auto batch = next_batch (); !batch.empty ())
		{
			for (auto const & [entry, type] : batch)
			{
				auto const & [buffer, callback] = entry;

				auto const result = co_await send_one (type, buffer);
				if (!result)
				{
					node.stats.inc (nano::stat::type::tcp_channel_ec, nano::to_stat_detail (result.error ()), nano::stat::dir::out);
				}

				// The one place a send is reported, whichever step it ended at
				if (callback)
				{
					callback (result.error ()); // Empty on success
				}

				if (!result)
				{
					co_return result.error (); // Stop on error
				}
			}
		}
		else
		{
			co_await condition.wait ();
		}
	}

	co_return nano::io_success ();
}

auto nano::transport::tcp_channel::send_one (nano::transport::traffic_type type, nano::shared_const_buffer const & buffer) -> asio::awaitable<nano::io_result<void>>
{
	debug_assert (strand.running_in_this_thread ());

	auto const size = buffer.size ();

	// Waits only when the bandwidth left over from earlier sends does not cover this one
	if (allocated_bandwidth < size)
	{
		auto const waited = co_await wait_bandwidth (type, size);
		if (!waited)
		{
			co_return waited.error ();
		}
	}

	if (allocated_bandwidth < size)
	{
		// A finished wait leaves enough allocated, so a shortfall is a bug, and the send fails rather than get past the limiter
		debug_assert (false, "outbound bandwidth not allocated");
		co_return asio::error::no_buffer_space;
	}
	else
	{
		// The send's bytes come out of the allocation
		allocated_bandwidth -= size;
	}

	node.stats.inc (nano::stat::type::tcp_channel, nano::stat::detail::send, nano::stat::dir::out);
	node.stats.inc (nano::stat::type::tcp_channel_send, to_stat_detail (type), nano::stat::dir::out);

	auto const written = co_await socket->co_write (buffer, buffer.size ());
	debug_assert (strand.running_in_this_thread ());

	if (!written)
	{
		co_return written.error ();
	}

	node.stats.add (nano::stat::type::traffic_tcp_type, to_stat_detail (type), nano::stat::dir::out, size);
	set_last_packet_sent (std::chrono::steady_clock::now ());

	co_return nano::io_success ();
}

auto nano::transport::tcp_channel::wait_bandwidth (nano::transport::traffic_type type, size_t size) -> asio::awaitable<nano::io_result<void>>
{
	debug_assert (strand.running_in_this_thread ());

	while (allocated_bandwidth < size)
	{
		// Every write to a closed socket fails, so waiting on would only delay reporting the send
		if (!socket->alive ())
		{
			co_return asio::error::operation_aborted;
		}

		// What this send still lacks, never zero while the loop runs
		auto const missing = size - allocated_bandwidth;
		debug_assert (missing <= size, "missing bandwidth wrapped around");

		// Allocated in larger amounts to reduce the number of limiter calls, but at least what is missing, so a small or zero setting still makes progress
		auto const amount = std::max (node.config.tcp->bandwidth_allocation, missing);
		if (auto result = node.outbound_limiter.consume (amount, type))
		{
			allocated_bandwidth += amount;
		}
		else
		{
			node.stats.inc (nano::stat::type::tcp_channel_wait, nano::stat::detail::wait_bandwidth, nano::stat::dir::out);

			// Cap the wait so channel teardown is not delayed by long refill times
			auto const slept = co_await nano::async::sleep_for (std::min<std::chrono::milliseconds> (result.retry_after, 100ms));

			// A cancelled wait ends here with its error: the coroutine's next wait would throw, and the send would never be reported
			if (!slept)
			{
				co_return slept.error ();
			}
		}
	}

	co_return nano::io_success ();
}

bool nano::transport::tcp_channel::alive () const
{
	return socket->alive ();
}

nano::endpoint nano::transport::tcp_channel::get_remote_endpoint () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return remote_endpoint;
}

nano::endpoint nano::transport::tcp_channel::get_local_endpoint () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return local_endpoint;
}

std::string nano::transport::tcp_channel::to_string () const
{
	return nano::util::to_str (get_remote_endpoint ());
}

void nano::transport::tcp_channel::operator() (nano::object_stream & obs) const
{
	nano::transport::channel::operator() (obs); // Write common data
	obs.write ("socket", socket);
}
