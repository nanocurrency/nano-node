#include <celerix/lib/stacktrace.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/transport/message_deserializer.hpp>
#include <celerix/node/transport/tcp_channel.hpp>
#include <celerix/node/transport/transport.hpp>

/*
 * tcp_channel
 */

celerix::transport::tcp_channel::tcp_channel (celerix::node & node_a, std::shared_ptr<celerix::transport::tcp_socket> socket_a) :
	channel (node_a),
	socket{ socket_a },
	strand{ node_a.io_ctx.get_executor () },
	sending_task{ strand }
{
	stacktrace = celerix::generate_stacktrace ();
	remote_endpoint = socket_a->remote_endpoint ();
	local_endpoint = socket_a->local_endpoint ();
	start ();
}

celerix::transport::tcp_channel::~tcp_channel ()
{
	close ();
	release_assert (!sending_task.joinable ());
}

void celerix::transport::tcp_channel::close ()
{
	stop ();
	socket->close ();
	closed = true;
}

void celerix::transport::tcp_channel::start ()
{
	sending_task = celerix::async::task (strand, [this] (celerix::async::condition & condition) {
		return start_sending (condition); // This is not a coroutine, but a corotuine factory
	});
}

asio::awaitable<void> celerix::transport::tcp_channel::start_sending (celerix::async::condition & condition)
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

void celerix::transport::tcp_channel::stop ()
{
	if (sending_task.joinable ())
	{
		// Node context must be running to gracefully stop async tasks
		debug_assert (!node.io_ctx.stopped ());
		// Ensure that we are not trying to await the task while running on the same thread / io_context
		debug_assert (!node.io_ctx.get_executor ().running_in_this_thread ());
		sending_task.cancel ();
		sending_task.join ();
	}
}

bool celerix::transport::tcp_channel::max (celerix::transport::traffic_type traffic_type)
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return queue.max (traffic_type);
}

bool celerix::transport::tcp_channel::send_buffer (celerix::shared_const_buffer const & buffer, celerix::transport::traffic_type type, celerix::transport::channel::callback_t callback)
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	if (!queue.full (type))
	{
		queue.push (type, { buffer, callback });
		lock.unlock ();
		node.stats.inc (celerix::stat::type::tcp_channel, celerix::stat::detail::queued, celerix::stat::dir::out);
		node.stats.inc (celerix::stat::type::tcp_channel_queued, to_stat_detail (type), celerix::stat::dir::out);
		sending_task.notify ();
		return true;
	}
	else
	{
		node.stats.inc (celerix::stat::type::tcp_channel, celerix::stat::detail::drop, celerix::stat::dir::out);
		node.stats.inc (celerix::stat::type::tcp_channel_drop, to_stat_detail (type), celerix::stat::dir::out);
	}
	return false;
}

asio::awaitable<void> celerix::transport::tcp_channel::run_sending (celerix::async::condition & condition)
{
	while (!co_await celerix::async::cancelled ())
	{
		debug_assert (strand.running_in_this_thread ());

		auto next_batch = [this] () {
			const size_t max_batch = 8; // TODO: Make this configurable
			celerix::lock_guard<celerix::mutex> lock{ mutex };
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

asio::awaitable<void> celerix::transport::tcp_channel::send_one (traffic_type type, tcp_channel_queue::entry_t const & item)
{
	debug_assert (strand.running_in_this_thread ());

	auto const & [buffer, callback] = item;
	auto const size = buffer.size ();

	// Wait for socket
	while (socket->full ())
	{
		node.stats.inc (celerix::stat::type::tcp_channel_wait, celerix::stat::detail::wait_socket, celerix::stat::dir::out);
		co_await celerix::async::sleep_for (100ms); // TODO: Exponential backoff
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
			node.stats.inc (celerix::stat::type::tcp_channel_wait, celerix::stat::detail::wait_bandwidth, celerix::stat::dir::out);
			co_await celerix::async::sleep_for (100ms); // TODO: Exponential backoff
		}
	}
	allocated_bandwidth -= size;

	node.stats.inc (celerix::stat::type::tcp_channel, celerix::stat::detail::send, celerix::stat::dir::out);
	node.stats.inc (celerix::stat::type::tcp_channel_send, to_stat_detail (type), celerix::stat::dir::out);

	socket->async_write (buffer, [this_w = weak_from_this (), callback, type] (boost::system::error_code const & ec, std::size_t size) {
		if (auto this_l = this_w.lock ())
		{
			this_l->node.stats.inc (celerix::stat::type::tcp_channel_ec, celerix::to_stat_detail (ec), celerix::stat::dir::out);
			if (!ec)
			{
				this_l->node.stats.add (celerix::stat::type::traffic_tcp_type, to_stat_detail (type), celerix::stat::dir::out, size);
				this_l->set_last_packet_sent (std::chrono::steady_clock::now ());
			}
		}
		if (callback)
		{
			callback (ec, size);
		}
	});
}

bool celerix::transport::tcp_channel::alive () const
{
	return socket->alive ();
}

celerix::endpoint celerix::transport::tcp_channel::get_remote_endpoint () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return remote_endpoint;
}

celerix::endpoint celerix::transport::tcp_channel::get_local_endpoint () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return local_endpoint;
}

std::string celerix::transport::tcp_channel::to_string () const
{
	return celerix::util::to_str (get_remote_endpoint ());
}

void celerix::transport::tcp_channel::operator() (celerix::object_stream & obs) const
{
	celerix::transport::channel::operator() (obs); // Write common data
	obs.write ("socket", socket);
}

/*
 * tcp_channel_queue
 */

celerix::transport::tcp_channel_queue::tcp_channel_queue ()
{
	for (auto type : all_traffic_types ())
	{
		queues.at (type) = { type, {} };
	}
}

bool celerix::transport::tcp_channel_queue::empty () const
{
	return std::all_of (queues.begin (), queues.end (), [] (auto const & queue) {
		return queue.second.empty ();
	});
}

size_t celerix::transport::tcp_channel_queue::size () const
{
	return std::accumulate (queues.begin (), queues.end (), size_t{ 0 }, [] (size_t acc, auto const & queue) {
		return acc + queue.second.size ();
	});
}

size_t celerix::transport::tcp_channel_queue::size (traffic_type type) const
{
	return queues.at (type).second.size ();
}

bool celerix::transport::tcp_channel_queue::max (traffic_type type) const
{
	return size (type) >= max_size;
}

bool celerix::transport::tcp_channel_queue::full (traffic_type type) const
{
	return size (type) >= full_size;
}

void celerix::transport::tcp_channel_queue::push (traffic_type type, entry_t entry)
{
	debug_assert (!full (type)); // Should be checked before calling this function
	queues.at (type).second.push_back (entry);
}

auto celerix::transport::tcp_channel_queue::next () -> value_t
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

auto celerix::transport::tcp_channel_queue::next_batch (size_t max_count) -> batch_t
{
	// TODO: Naive implementation, could be optimized
	std::deque<value_t> result;
	while (!empty () && result.size () < max_count)
	{
		result.emplace_back (next ());
	}
	return result;
}

size_t celerix::transport::tcp_channel_queue::priority (traffic_type type) const
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

void celerix::transport::tcp_channel_queue::seek_next ()
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
