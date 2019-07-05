#pragma once

#include <boost/asio.hpp>
#include <boost/optional.hpp>

#include <chrono>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

namespace nano
{
class node;
class server_socket;

/** Socket class for tcp clients and newly accepted connections */
class socket : public std::enable_shared_from_this<nano::socket>
{
	friend class server_socket;

public:
	/**
	 * If multi_writer is used, overlapping writes are allowed, including from multiple threads.
	 * For bootstrapping, reading and writing alternates on a socket, thus single_writer
	 * should be used to avoid queueing overhead. For live messages, multiple threads may want
	 * to concurrenctly queue messages on the same socket, thus multi_writer should be used.
	 */
	enum class concurrency
	{
		single_writer,
		multi_writer
	};

	/**
	 * Constructor
	 * @param node Owning node
	 * @param io_timeout If tcp async operation is not completed within the timeout, the socket is closed. If not set, the tcp_io_timeout config option is used.
	 * @param concurrency write concurrency
	 */
	explicit socket (std::shared_ptr<nano::node> node, boost::optional<std::chrono::seconds> io_timeout = boost::none, concurrency = concurrency::single_writer);
	virtual ~socket ();
	void async_connect (boost::asio::ip::tcp::endpoint const &, std::function<void(boost::system::error_code const &)>);
	void async_read (std::shared_ptr<std::vector<uint8_t>>, size_t, std::function<void(boost::system::error_code const &, size_t)>);
	void async_write (std::shared_ptr<std::vector<uint8_t>>, std::function<void(boost::system::error_code const &, size_t)> = nullptr);

	void close ();
	boost::asio::ip::tcp::endpoint remote_endpoint () const;
	/** Returns true if the socket has timed out */
	bool has_timed_out () const;
	/** This can be called to change the maximum idle time, e.g. based on the type of traffic detected. */
	void set_timeout (std::chrono::seconds io_timeout_a);
	void start_timer (std::chrono::seconds deadline_a);
	/** Change write concurrent */
	void set_writer_concurrency (concurrency writer_concurrency_a);

protected:
	/** Holds the buffer and callback for queued writes */
	class queue_item
	{
	public:
		std::shared_ptr<std::vector<uint8_t>> buffer;
		std::function<void(boost::system::error_code const &, size_t)> callback;
	};

	boost::asio::strand<boost::asio::io_context::executor_type> strand;
	boost::asio::ip::tcp::socket tcp_socket;
	std::weak_ptr<nano::node> node;

	/** The other end of the connection */
	boost::asio::ip::tcp::endpoint remote;
	/** Send queue, protected by always being accessed in the strand */
	std::deque<queue_item> send_queue;
	std::atomic<concurrency> writer_concurrency;

	std::atomic<uint64_t> next_deadline;
	std::atomic<uint64_t> last_completion_time;
	std::atomic<bool> timed_out{ false };
	boost::optional<std::chrono::seconds> io_timeout;
	size_t const queue_size_max = 128;

	/** Set by close() - completion handlers must check this. This is more reliable than checking
	 error codes as the OS may have already completed the async operation. */
	std::atomic<bool> closed{ false };
	void close_internal ();
	void write_queued_messages ();
	void start_timer ();
	void stop_timer ();
	void checkup ();
};

/** Socket class for TCP servers */
class server_socket final : public socket
{
public:
	/**
	 * Constructor
	 * @param node_a Owning node
	 * @param local_a Address and port to listen on
	 * @param max_connections_a Maximum number of concurrent connections
	 * @param concurrency_a Write concurrency for new connections
	 */
	explicit server_socket (std::shared_ptr<nano::node> node_a, boost::asio::ip::tcp::endpoint local_a, size_t max_connections_a, concurrency concurrency_a = concurrency::single_writer);
	/**Start accepting new connections */
	void start (boost::system::error_code &);
	/** Stop accepting new connections */
	void close ();
	/** Register callback for new connections. The callback must return true to keep accepting new connections. */
	void on_connection (std::function<bool(std::shared_ptr<nano::socket> new_connection, boost::system::error_code const &)>);
	uint16_t listening_port ()
	{
		return local.port ();
	}

private:
	std::vector<std::weak_ptr<nano::socket>> connections;
	boost::asio::ip::tcp::acceptor acceptor;
	boost::asio::ip::tcp::endpoint local;
	boost::asio::steady_timer deferred_accept_timer;
	size_t max_inbound_connections;
	/** Concurrency setting for new connections */
	concurrency concurrency_new_connections;
	void evict_dead_connections ();
};
}
