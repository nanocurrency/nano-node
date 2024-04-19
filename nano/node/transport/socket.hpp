#pragma once

#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/boost/asio/strand.hpp>
#include <nano/lib/asio.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/transport/traffic_type.hpp>

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

namespace nano
{
class node;
}

namespace nano::transport
{
/** Policy to affect at which stage a buffer can be dropped */
enum class buffer_drop_policy
{
	/** Can be dropped by bandwidth limiter (default) */
	limiter,
	/** Should not be dropped by bandwidth limiter */
	no_limiter_drop,
	/** Should not be dropped by bandwidth limiter or socket write queue limiter */
	no_socket_drop
};

enum class socket_type
{
	undefined,
	bootstrap,
	realtime,
	realtime_response_server // special type for tcp channel response server
};

std::string_view to_string (socket_type);

enum class socket_endpoint
{
	server, // Socket was created by accepting an incoming connection
	client, // Socket was created by initiating an outgoing connection
};

std::string_view to_string (socket_endpoint);

/** Socket class for tcp clients and newly accepted connections */
class socket final : public std::enable_shared_from_this<socket>
{
	friend class tcp_server;
	friend class tcp_channels;
	friend class tcp_listener;

public:
	static std::size_t constexpr default_max_queue_size = 128;

public:
	explicit socket (nano::node &, nano::transport::socket_endpoint = socket_endpoint::client, std::size_t max_queue_size = default_max_queue_size);

	// TODO: Accepting remote/local endpoints as a parameter is unnecessary, but is needed for now to keep compatibility with the legacy code
	explicit socket (
	nano::node &,
	boost::asio::ip::tcp::socket,
	boost::asio::ip::tcp::endpoint remote_endpoint,
	boost::asio::ip::tcp::endpoint local_endpoint,
	nano::transport::socket_endpoint = socket_endpoint::server,
	std::size_t max_queue_size = default_max_queue_size);

	~socket ();

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
	nano::shared_const_buffer const &,
	std::function<void (boost::system::error_code const &, std::size_t)> callback = {},
	traffic_type = traffic_type::generic);

	boost::asio::ip::tcp::endpoint remote_endpoint () const;
	boost::asio::ip::tcp::endpoint local_endpoint () const;

	/** Returns true if the socket has timed out */
	bool has_timed_out () const;
	/** This can be called to change the maximum idle time, e.g. based on the type of traffic detected. */
	void set_default_timeout_value (std::chrono::seconds);
	std::chrono::seconds get_default_timeout_value () const;
	void set_timeout (std::chrono::seconds);

	bool max (nano::transport::traffic_type = traffic_type::generic) const;
	bool full (nano::transport::traffic_type = traffic_type::generic) const;

	nano::transport::socket_type type () const
	{
		return type_m;
	};
	void type_set (nano::transport::socket_type type_a)
	{
		type_m = type_a;
	}
	nano::transport::socket_endpoint endpoint_type () const
	{
		return endpoint_type_m;
	}
	bool is_realtime_connection () const
	{
		return type () == socket_type::realtime || type () == socket_type::realtime_response_server;
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
	class write_queue
	{
	public:
		using buffer_t = nano::shared_const_buffer;
		using callback_t = std::function<void (boost::system::error_code const &, std::size_t)>;

		struct entry
		{
			buffer_t buffer;
			callback_t callback;
		};

	public:
		explicit write_queue (std::size_t max_size);

		bool insert (buffer_t const &, callback_t, nano::transport::traffic_type);
		std::optional<entry> pop ();
		void clear ();
		std::size_t size (nano::transport::traffic_type) const;
		bool empty () const;

		std::size_t const max_size;

	private:
		mutable nano::mutex mutex;
		std::unordered_map<nano::transport::traffic_type, std::queue<entry>> queues;
	};

	write_queue send_queue;

protected:
	std::weak_ptr<nano::node> node_w;

	boost::asio::strand<boost::asio::io_context::executor_type> strand;
	boost::asio::ip::tcp::socket tcp_socket;

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
	std::atomic<nano::seconds_t> last_receive_time_or_init;

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
	nano::transport::socket_type type_m{ socket_type::undefined };
	nano::transport::socket_endpoint endpoint_type_m;

public:
	std::size_t const max_queue_size;

public: // Logging
	virtual void operator() (nano::object_stream &) const;
};

using address_socket_mmap = std::multimap<boost::asio::ip::address, std::weak_ptr<socket>>;

namespace socket_functions
{
	boost::asio::ip::network_v6 get_ipv6_subnet_address (boost::asio::ip::address_v6 const &, std::size_t);
	boost::asio::ip::address first_ipv6_subnet_address (boost::asio::ip::address_v6 const &, std::size_t);
	boost::asio::ip::address last_ipv6_subnet_address (boost::asio::ip::address_v6 const &, std::size_t);
	std::size_t count_subnetwork_connections (nano::transport::address_socket_mmap const &, boost::asio::ip::address_v6 const &, std::size_t);
}
}
