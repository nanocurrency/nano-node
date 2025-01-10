#pragma once

#include <celerix/boost/asio/ip/tcp.hpp>
#include <celerix/boost/asio/strand.hpp>
#include <celerix/lib/asio.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/node/transport/common.hpp>
#include <celerix/node/transport/traffic_type.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace boost::asio::ip
{
class network_v6;
}

namespace celerix
{
class node;
}

namespace celerix::transport
{
class socket_queue final
{
public:
	using buffer_t = celerix::shared_const_buffer;
	using callback_t = std::function<void (boost::system::error_code const &, std::size_t)>;

	struct entry
	{
		buffer_t buffer;
		callback_t callback;
	};

public:
	using result_t = std::pair<entry, celerix::transport::traffic_type>;

	explicit socket_queue (std::size_t max_size);

	bool insert (buffer_t const &, callback_t, celerix::transport::traffic_type);
	std::optional<result_t> pop ();
	void clear ();
	std::size_t size (celerix::transport::traffic_type) const;
	bool empty () const;

	std::size_t const max_size;

private:
	mutable celerix::mutex mutex;
	std::unordered_map<celerix::transport::traffic_type, std::queue<entry>> queues;
};

/** Socket class for tcp clients and newly accepted connections */
class tcp_socket final : public std::enable_shared_from_this<tcp_socket>
{
	friend class tcp_server;
	friend class tcp_channels;
	friend class tcp_listener;

public:
	static size_t constexpr default_queue_size = 16;

public:
	explicit tcp_socket (celerix::node &, celerix::transport::socket_endpoint = socket_endpoint::client, size_t queue_size = default_queue_size);

	// TODO: Accepting remote/local endpoints as a parameter is unnecessary, but is needed for now to keep compatibility with the legacy code
	tcp_socket (
	celerix::node &,
	boost::asio::ip::tcp::socket,
	boost::asio::ip::tcp::endpoint remote_endpoint,
	boost::asio::ip::tcp::endpoint local_endpoint,
	celerix::transport::socket_endpoint = socket_endpoint::server,
	size_t queue_size = default_queue_size);

	~tcp_socket ();

	void start ();
	void close ();

	void async_connect (
	boost::asio::ip::tcp::endpoint const & endpoint,
	std::function<void (boost::system::error_code const &)> callback);

	void async_read (
	std::shared_ptr<std::vector<uint8_t>> const & buffer,
	std::size_t size,
	std::function<void (boost::system::error_code const &, std::size_t)> callback);

	void async_write (
	celerix::shared_const_buffer const &,
	std::function<void (boost::system::error_code const &, std::size_t)> callback = nullptr);

	boost::asio::ip::tcp::endpoint remote_endpoint () const;
	boost::asio::ip::tcp::endpoint local_endpoint () const;

	/** Returns true if the socket has timed out */
	bool has_timed_out () const;
	/** This can be called to change the maximum idle time, e.g. based on the type of traffic detected. */
	void set_default_timeout_value (std::chrono::seconds);
	std::chrono::seconds get_default_timeout_value () const;
	void set_timeout (std::chrono::seconds);

	bool max () const;
	bool full () const;

	celerix::transport::socket_type type () const
	{
		return type_m;
	};
	void type_set (celerix::transport::socket_type type)
	{
		type_m = type;
	}
	celerix::transport::socket_endpoint endpoint_type () const
	{
		return endpoint_type_m;
	}
	bool is_realtime_connection () const
	{
		return type () == socket_type::realtime;
	}
	bool is_bootstrap_connection () const
	{
		return type () == socket_type::bootstrap;
	}
	bool is_closed () const
	{
		return closed;
	}
	bool alive () const
	{
		return !is_closed ();
	}

private:
	size_t const queue_size;
	socket_queue send_queue;

protected:
	std::weak_ptr<celerix::node> node_w;

	boost::asio::strand<boost::asio::io_context::executor_type> strand;
	boost::asio::ip::tcp::socket raw_socket;

	/** The other end of the connection */
	boost::asio::ip::tcp::endpoint remote;
	boost::asio::ip::tcp::endpoint local;

	/** number of seconds of inactivity that causes a socket timeout
	 *  activity is any successful connect, send or receive event
	 */
	std::atomic<uint64_t> timeout;

	/** the timestamp (in seconds since epoch) of the last time there was successful activity on the socket
	 *  activity is any successful connect, send or receive event
	 */
	std::atomic<uint64_t> last_completion_time_or_init;

	/** the timestamp (in seconds since epoch) of the last time there was successful receive on the socket
	 *  successful receive includes graceful closing of the socket by the peer (the read succeeds but returns 0 bytes)
	 */
	std::atomic<celerix::seconds_t> last_receive_time_or_init;

	/** Flag that is set when cleanup decides to close the socket due to timeout.
	 *  NOTE: Currently used by tcp_server::timeout() but I suspect that this and tcp_server::timeout() are not needed.
	 */
	std::atomic<bool> timed_out{ false };

	/** the timeout value to use when calling set_default_timeout() */
	std::atomic<std::chrono::seconds> default_timeout;

	/** used in real time server sockets, number of seconds of no receive traffic that will cause the socket to timeout */
	std::chrono::seconds silent_connection_tolerance_time;

	/** Set by close() - completion handlers must check this. This is more reliable than checking
	 error codes as the OS may have already completed the async operation. */
	std::atomic<bool> closed{ false };

	/** Updated only from strand, but stored as atomic so it can be read from outside */
	std::atomic<bool> write_in_progress{ false };

	void close_internal ();
	void write_queued_messages ();
	void set_default_timeout ();
	void set_last_completion ();
	void set_last_receive_time ();
	void ongoing_checkup ();
	void read_impl (std::shared_ptr<std::vector<uint8_t>> const & data_a, std::size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a);

private:
	socket_endpoint const endpoint_type_m;
	std::atomic<socket_type> type_m{ socket_type::undefined };

public: // Logging
	virtual void operator() (celerix::object_stream &) const;
};
}
