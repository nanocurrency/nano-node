#include <nano/lib/stacktrace.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/message_deserializer.hpp>
#include <nano/node/transport/tcp_channel.hpp>
#include <nano/node/transport/transport.hpp>

/*
 * tcp_channel
 */

nano::transport::tcp_channel::tcp_channel (nano::node & node_a, std::shared_ptr<nano::transport::tcp_socket> socket_a) :
	channel (node_a),
	socket{ socket_a },
	strand{ node_a.io_ctx.get_executor () },
	sending_task{ strand }
{
	remote_endpoint = socket_a->remote_endpoint ();
	local_endpoint = socket_a->local_endpoint ();
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
	closed = true;
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
	try
	{
		co_await run_sending (condition);
	}
	catch (boost::system::system_error const & ex)
	{
		// Operation aborted is expected when cancelling the acceptor
		debug_assert (ex.code () == asio::error::operation_aborted);
	}
	debug_assert (strand.running_in_this_thread ());
}

void nano::transport::tcp_channel::stop ()
{
	if (sending_task.running ())
	{
		// Node context must be running to gracefully stop async tasks
		debug_assert (!node.io_ctx.stopped ());
		// Ensure that we are not trying to await the task while running on the same thread / io_context
		debug_assert (!node.io_ctx.get_executor ().running_in_this_thread ());
		sending_task.cancel ();
		sending_task.join ();
	}
}

bool nano::transport::tcp_channel::max (nano::transport::traffic_type traffic_type)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return queue.max (traffic_type);
}

bool nano::transport::tcp_channel::send_impl (nano::message const & message, nano::transport::traffic_type type, nano::transport::channel::callback_t callback)
{
	auto buffer = message.to_shared_const_buffer ();

	nano::unique_lock<nano::mutex> lock{ mutex };
	if (!queue.full (type))
	{
		queue.push (type, { buffer, callback });
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

asio::awaitable<void> nano::transport::tcp_channel::run_sending (nano::async::condition & condition)
{
	while (!co_await nano::async::cancelled ())
	{
		debug_assert (strand.running_in_this_thread ());

		auto next_batch = [this] () {
			const size_t max_batch = 8; // TODO: Make this configurable
			nano::lock_guard<nano::mutex> lock{ mutex };
			return queue.next_batch (max_batch);
		};

		if (auto batch = next_batch (); !batch.empty ())
		{
			for (auto const & [type, item] : batch)
			{
				co_await send_one (type, item);
			}
		}
		else
		{
			co_await condition.wait ();
		}
	}
}

asio::awaitable<void> nano::transport::tcp_channel::send_one (traffic_type type, tcp_channel_queue::entry_t const & item)
{
	debug_assert (strand.running_in_this_thread ());

	auto const & [buffer, callback] = item;
	auto const size = buffer.size ();

	// Wait for socket
	while (socket->full ())
	{
		node.stats.inc (nano::stat::type::tcp_channel_wait, nano::stat::detail::wait_socket, nano::stat::dir::out);
		co_await nano::async::sleep_for (100ms); // TODO: Exponential backoff
	}

	// Wait for bandwidth
	// This is somewhat inefficient
	// The performance impact *should* be mitigated by the fact that we allocate it in larger chunks, so this happens relatively infrequently
	const size_t bandwidth_chunk = 128 * 1024; // TODO: Make this configurable
	while (allocated_bandwidth < size)
	{
		// TODO: Consider implementing a subsribe/notification mechanism for bandwidth allocation
		if (node.outbound_limiter.should_pass (bandwidth_chunk, type)) // Allocate bandwidth in larger chunks
		{
			allocated_bandwidth += bandwidth_chunk;
		}
		else
		{
			node.stats.inc (nano::stat::type::tcp_channel_wait, nano::stat::detail::wait_bandwidth, nano::stat::dir::out);
			co_await nano::async::sleep_for (100ms); // TODO: Exponential backoff
		}
	}
	allocated_bandwidth -= size;

	node.stats.inc (nano::stat::type::tcp_channel, nano::stat::detail::send, nano::stat::dir::out);
	node.stats.inc (nano::stat::type::tcp_channel_send, to_stat_detail (type), nano::stat::dir::out);

	socket->async_write (buffer, [this_w = weak_from_this (), callback, type] (boost::system::error_code const & ec, std::size_t size) {
		if (auto this_l = this_w.lock ())
		{
			this_l->node.stats.inc (nano::stat::type::tcp_channel_ec, nano::to_stat_detail (ec), nano::stat::dir::out);
			if (!ec)
			{
				this_l->node.stats.add (nano::stat::type::traffic_tcp_type, to_stat_detail (type), nano::stat::dir::out, size);
				this_l->set_last_packet_sent (std::chrono::steady_clock::now ());
			}
		}
		if (callback)
		{
			callback (ec, size);
		}
	});
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

/*
 * tcp_channel_queue
 */

nano::transport::tcp_channel_queue::tcp_channel_queue ()
{
	for (auto type : all_traffic_types ())
	{
		queues.at (type) = { type, {} };
	}
}

bool nano::transport::tcp_channel_queue::empty () const
{
	return std::all_of (queues.begin (), queues.end (), [] (auto const & queue) {
		return queue.second.empty ();
	});
}

size_t nano::transport::tcp_channel_queue::size () const
{
	return std::accumulate (queues.begin (), queues.end (), size_t{ 0 }, [] (size_t acc, auto const & queue) {
		return acc + queue.second.size ();
	});
}

size_t nano::transport::tcp_channel_queue::size (traffic_type type) const
{
	return queues.at (type).second.size ();
}

bool nano::transport::tcp_channel_queue::max (traffic_type type) const
{
	return size (type) >= max_size;
}

bool nano::transport::tcp_channel_queue::full (traffic_type type) const
{
	return size (type) >= full_size;
}

void nano::transport::tcp_channel_queue::push (traffic_type type, entry_t entry)
{
	debug_assert (!full (type)); // Should be checked before calling this function
	queues.at (type).second.push_back (entry);
}

auto nano::transport::tcp_channel_queue::next () -> value_t
{
	debug_assert (!empty ()); // Should be checked before calling next

	auto should_seek = [&, this] () {
		if (current == queues.end ())
		{
			return true;
		}
		auto & queue = current->second;
		if (queue.empty ())
		{
			return true;
		}
		// Allow up to `priority` requests to be processed before moving to the next queue
		if (counter >= priority (current->first))
		{
			return true;
		}
		return false;
	};

	if (should_seek ())
	{
		seek_next ();
	}

	release_assert (current != queues.end ());

	auto & source = current->first;
	auto & queue = current->second;

	++counter;

	release_assert (!queue.empty ());
	auto entry = queue.front ();
	queue.pop_front ();
	return { source, entry };
}

auto nano::transport::tcp_channel_queue::next_batch (size_t max_count) -> batch_t
{
	// TODO: Naive implementation, could be optimized
	std::deque<value_t> result;
	while (!empty () && result.size () < max_count)
	{
		result.emplace_back (next ());
	}
	return result;
}

size_t nano::transport::tcp_channel_queue::priority (traffic_type type) const
{
	switch (type)
	{
		case traffic_type::block_broadcast:
		case traffic_type::vote_rebroadcast:
			return 1;
		default:
			return 4;
	}
}

void nano::transport::tcp_channel_queue::seek_next ()
{
	counter = 0;
	do
	{
		if (current != queues.end ())
		{
			++current;
		}
		if (current == queues.end ())
		{
			current = queues.begin ();
		}
		release_assert (current != queues.end ());
	} while (current->second.empty ());
}
