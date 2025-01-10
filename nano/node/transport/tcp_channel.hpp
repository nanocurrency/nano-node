#pragma once

#include <celerix/lib/async.hpp>
#include <celerix/lib/enum_util.hpp>
#include <celerix/node/transport/channel.hpp>
#include <celerix/node/transport/fwd.hpp>
#include <celerix/node/transport/transport.hpp>

namespace celerix::transport
{
class tcp_channel_queue final
{
public:
	explicit tcp_channel_queue ();

	using callback_t = std::function<void (boost::system::error_code const &, std::size_t)>;
	using entry_t = std::pair<celerix::shared_const_buffer, callback_t>;
	using value_t = std::pair<traffic_type, entry_t>;
	using batch_t = std::deque<value_t>;

	bool empty () const;
	size_t size () const;
	size_t size (traffic_type) const;
	void push (traffic_type, entry_t);
	value_t next ();
	batch_t next_batch (size_t max_count);

	bool max (traffic_type) const;
	bool full (traffic_type) const;

public:
	constexpr static size_t max_size = 32;
	constexpr static size_t full_size = 4 * max_size;

private:
	void seek_next ();
	size_t priority (traffic_type) const;

	using queue_t = std::pair<traffic_type, std::deque<entry_t>>;
	celerix::enum_array<traffic_type, queue_t> queues{};
	celerix::enum_array<traffic_type, queue_t>::iterator current{ queues.end () };
	size_t counter{ 0 };
};

class tcp_channel final : public celerix::transport::channel, public std::enable_shared_from_this<tcp_channel>
{
	friend class celerix::transport::tcp_channels;

public:
	tcp_channel (celerix::node &, std::shared_ptr<celerix::transport::tcp_socket>);
	~tcp_channel () override;

	void close () override;

	bool max (celerix::transport::traffic_type traffic_type) override;
	bool alive () const override;

	celerix::endpoint get_remote_endpoint () const override;
	celerix::endpoint get_local_endpoint () const override;

	celerix::transport::transport_type get_type () const override
	{
		return celerix::transport::transport_type::tcp;
	}

	std::string to_string () const override;

protected:
	bool send_buffer (celerix::shared_const_buffer const &, celerix::transport::traffic_type, celerix::transport::channel::callback_t) override;

private:
	void start ();
	void stop ();

	asio::awaitable<void> start_sending (celerix::async::condition &);
	asio::awaitable<void> run_sending (celerix::async::condition &);
	asio::awaitable<void> send_one (traffic_type, tcp_channel_queue::entry_t const &);

public:
	std::shared_ptr<celerix::transport::tcp_socket> socket;

private:
	celerix::endpoint remote_endpoint;
	celerix::endpoint local_endpoint;

	celerix::async::strand strand;
	celerix::async::task sending_task;

	mutable celerix::mutex mutex;
	tcp_channel_queue queue;
	std::atomic<size_t> allocated_bandwidth{ 0 };

	// Debugging
	std::atomic<bool> closed{ false };
	std::string stacktrace;

public: // Logging
	void operator() (celerix::object_stream &) const override;
};
}
