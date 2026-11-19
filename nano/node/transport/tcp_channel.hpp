#pragma once

#include <nano/lib/async.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/node/enum_fair_queue.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano::transport
{
// A message waiting in a channel to be sent, with the callback to run once it is written or has failed
using traffic_entry = std::pair<nano::shared_const_buffer, nano::transport::send_callback>;
// The messages a channel waits to send, served round robin by traffic type and popped by the sender a batch at a time
using traffic_queue = nano::enum_fair_queue<nano::transport::traffic_entry, nano::transport::traffic_type, /* batch capacity */ 8>;

class tcp_channel final : public nano::transport::channel, public std::enable_shared_from_this<tcp_channel>
{
public:
	// Messages of one traffic type queued before max () tells senders to hold that type back
	static constexpr size_t max_queue_size = 32;
	// Messages of one traffic type queued before more are dropped
	static constexpr size_t full_queue_size = 4 * max_queue_size;

public:
	tcp_channel (nano::node &, std::shared_ptr<nano::transport::tcp_socket>, nano::transport::peer_info const &);
	~tcp_channel () override;

	void close () override;
	void close_async (); // Safe to call from io context

	bool alive () const override;

	nano::endpoint get_remote_endpoint () const override;
	nano::endpoint get_local_endpoint () const override;

	nano::transport::transport_type get_type () const override
	{
		return nano::transport::transport_type::tcp;
	}

	std::string to_string () const override;

protected:
	bool send_buffer_impl (nano::shared_const_buffer const &, nano::transport::traffic_type, nano::transport::send_callback) override;
	bool max_impl (nano::transport::traffic_type) override;

private:
	void start ();
	void stop ();

	asio::awaitable<void> start_sending (nano::async::condition &);
	// Writes queued sends until cancelled or a write fails, returning that error
	asio::awaitable<nano::io_result<void>> run_sending (nano::async::condition &);
	asio::awaitable<nano::io_result<void>> send_one (traffic_type, nano::transport::traffic_entry const &);

public:
	std::shared_ptr<nano::transport::tcp_socket> socket;

private:
	nano::endpoint remote_endpoint;
	nano::endpoint local_endpoint;

	nano::async::strand & strand; // The socket's, whose operations run only on it
	nano::async::task sending_task;

	mutable nano::mutex mutex;
	nano::transport::traffic_queue queue;
	std::atomic<size_t> allocated_bandwidth{ 0 };

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
