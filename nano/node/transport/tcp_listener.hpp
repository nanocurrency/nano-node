#pragma once

#include <nano/lib/async.hpp>
#include <nano/node/common.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/common.hpp>

#include <boost/asio.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <list>
#include <string_view>
#include <thread>

namespace mi = boost::multi_index;
namespace asio = boost::asio;

namespace nano::transport
{
class tcp_config
{
public:
	explicit tcp_config (nano::network_constants const & network)
	{
		if (network.is_dev_network ())
		{
			max_inbound_connections = 128;
			max_outbound_connections = 128;
			max_attempts = 128;
			max_attempts_per_ip = 128;
			connect_timeout = std::chrono::seconds{ 5 };
		}
	}

public:
	size_t max_inbound_connections{ 2048 };
	size_t max_outbound_connections{ 2048 };
	size_t max_attempts{ 60 };
	size_t max_attempts_per_ip{ 1 };
	std::chrono::seconds connect_timeout{ 60 };
};

/**
 * Server side portion of tcp sessions. Listens for new socket connections and spawns tcp_server objects when connected.
 */
class tcp_listener final
{
public:
	enum class connection_type
	{
		inbound,
		outbound,
	};

public:
	tcp_listener (uint16_t port, tcp_config const &, nano::node &);
	~tcp_listener ();

	void start ();
	void stop ();

	/**
	 * @param port is optional, if 0 then default peering port is used
	 * @return true if connection attempt was initiated
	 */
	bool connect (asio::ip::address ip, uint16_t port = 0);

	nano::tcp_endpoint endpoint () const;

	size_t connection_count () const;
	size_t connection_count (connection_type) const;
	size_t attempt_count () const;
	size_t realtime_count () const;
	size_t bootstrap_count () const;

	std::vector<std::shared_ptr<nano::transport::tcp_socket>> sockets () const;
	std::vector<std::shared_ptr<nano::transport::tcp_server>> servers () const;

	nano::container_info container_info () const;

public: // Events
	using connection_accepted_event_t = nano::observer_set<std::shared_ptr<nano::transport::tcp_socket> const &, std::shared_ptr<nano::transport::tcp_server>>;
	connection_accepted_event_t connection_accepted;

private: // Dependencies
	tcp_config const & config;
	nano::node & node;
	nano::stats & stats;
	nano::logger & logger;

private:
	asio::awaitable<void> run ();
	asio::awaitable<void> wait_available_slots () const;

	void run_cleanup ();
	void cleanup ();
	void timeout ();

	enum class accept_result
	{
		invalid,
		accepted,
		rejected,
		error,
	};

	asio::awaitable<void> connect_impl (asio::ip::tcp::endpoint);
	asio::awaitable<asio::ip::tcp::socket> connect_socket (asio::ip::tcp::endpoint);

	struct accept_return
	{
		accept_result result;
		std::shared_ptr<nano::transport::tcp_socket> socket;
		std::shared_ptr<nano::transport::tcp_server> server;
	};

	accept_return accept_one (asio::ip::tcp::socket, connection_type);
	accept_result check_limits (asio::ip::address const & ip, connection_type);
	asio::awaitable<asio::ip::tcp::socket> accept_socket ();

	size_t count_per_type (connection_type) const;
	size_t count_per_ip (asio::ip::address const & ip) const;
	size_t count_per_subnetwork (asio::ip::address const & ip) const;
	size_t count_attempts (asio::ip::address const & ip) const;

private:
	struct connection
	{
		asio::ip::tcp::endpoint endpoint;
		std::weak_ptr<nano::transport::tcp_socket> socket;
		std::weak_ptr<nano::transport::tcp_server> server;

		asio::ip::address address () const
		{
			return endpoint.address ();
		}
	};

	struct attempt
	{
		asio::ip::tcp::endpoint endpoint;
		nano::async::task task;

		std::chrono::steady_clock::time_point const start{ std::chrono::steady_clock::now () };

		asio::ip::address address () const
		{
			return endpoint.address ();
		}
	};

private:
	uint16_t const port;

	std::list<connection> connections;
	std::list<attempt> attempts;

	nano::async::strand strand;

	asio::ip::tcp::acceptor acceptor;
	asio::ip::tcp::endpoint local;

	std::atomic<bool> stopped;
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	nano::async::task task;
	std::thread cleanup_thread;

private:
	static nano::stat::dir to_stat_dir (connection_type);
	static std::string_view to_string (connection_type);
	static nano::transport::socket_endpoint to_socket_endpoint (connection_type);
};
}