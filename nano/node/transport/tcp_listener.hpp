#pragma once

#include <nano/lib/async.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/endpoint_templ.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/common.hpp>
#include <nano/node/transport/tcp_config.hpp>

#include <boost/asio.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

namespace mi = boost::multi_index;
namespace asio = boost::asio;

namespace nano::transport
{
/**
 * The listener's connections, outbound connects in flight and cooldowns of closed connections, counted per type, per IP and per subnetwork against the limits.
 * Holds no lock and never touches the sockets and tasks it carries, the listener drives it under its own mutex and tests drive it directly
 */
class tcp_listener_index final
{
public:
	// Caps the index enforces, filled by the listener from its config and the node flags
	struct limits
	{
		size_t max_inbound{ 2048 }; // Accepted connections open at once
		size_t max_outbound{ 2048 }; // Initiated connections open at once
		size_t max_attempts{ 60 }; // Outbound connects in flight at once
		size_t max_attempts_per_ip{ 1 }; // Outbound connects in flight to one IP
		std::optional<size_t> max_per_ip{ 4 }; // Connections per IP, open or cooling down, empty leaves the limit unenforced
		std::optional<size_t> max_per_subnetwork{ 16 }; // Connections per subnetwork, open or cooling down, empty leaves the limit unenforced and IPv4 addresses are exempt regardless
		std::chrono::seconds reconnect_cooldown{ 60 }; // Time a closed connection keeps counting, 0 disables
	};

	enum class result
	{
		accepted,
		max_per_ip,
		max_per_subnetwork,
		max_inbound,
		max_outbound,
		max_attempts,
		max_attempts_per_ip,
	};

	struct connection
	{
		connection_type type; // Whether this node accepted or initiated it
		nano::tcp_endpoint endpoint; // Remote endpoint as connected
		asio::ip::address address; // Remote address normalized with ipv4_address_or_ipv6_subnet, the per IP key
		asio::ip::address subnetwork; // Remote subnetwork, the per subnetwork key
		std::shared_ptr<tcp_socket> socket; // Transport the connection runs on
		std::shared_ptr<tcp_server> server; // Handshake and message loop over the socket
	};

	struct attempt
	{
		nano::tcp_endpoint endpoint; // Endpoint being connected to
		asio::ip::address address; // Its normalized address, the per IP key
		std::shared_ptr<nano::async::task> task; // The connect in flight
		std::chrono::steady_clock::time_point start; // When the connect began
	};

	// Everything the index held when it was cleared
	struct contents
	{
		std::deque<connection> connections;
		std::deque<attempt> attempts;
	};

public:
	explicit tcp_listener_index (limits const &);

	// Whether a connection of the type to or from the address stays within the limits
	result check (connection_type, asio::ip::address const &) const;
	// Whether one more outbound connect to the address stays within the attempt limits
	result check_attempt (asio::ip::address const &) const;

	// Records an open connection
	void insert (connection_type, nano::tcp_endpoint const &, std::shared_ptr<tcp_socket>, std::shared_ptr<tcp_server>);
	// Records an outbound connect in flight
	void insert_attempt (nano::tcp_endpoint const &, std::shared_ptr<nano::async::task>, std::chrono::steady_clock::time_point now);
	// Removes the connections the predicate selects, starting their cooldowns, and hands them back for closing
	std::deque<connection> erase_if (std::function<bool (connection const &)> const &, std::chrono::steady_clock::time_point now);
	// Removes the attempts the predicate selects and hands them back
	std::deque<attempt> erase_attempts_if (std::function<bool (attempt const &)> const &);
	// Drops the cooldowns that have expired
	void update (std::chrono::steady_clock::time_point now);
	// Forgets every connection, attempt and cooldown and hands the first two back
	contents clear ();

	std::deque<connection> list_connections () const;
	std::deque<attempt> list_attempts () const;

	size_t size () const;
	size_t size (connection_type) const;
	size_t attempt_count () const;
	size_t cooldown_count () const;
	// Open connections plus cooldowns of the address, or of its subnetwork
	size_t count_per_ip (asio::ip::address const &) const;
	size_t count_per_subnetwork (asio::ip::address const &) const;
	// Outbound connects in flight to the address
	size_t count_attempts (asio::ip::address const &) const;

	nano::container_info container_info () const;

private:
	limits const config;

	struct cooldown
	{
		asio::ip::address address; // Normalized address of the closed connection, the per IP key
		asio::ip::address subnetwork; // Its subnetwork, the per subnetwork key
		std::chrono::steady_clock::time_point expiry; // When the slot is free again
	};

	// clang-format off
	class tag_sequenced {};
	class tag_type {};
	class tag_address {};
	class tag_subnetwork {};
	class tag_expiry {};

	using ordered_connections = boost::multi_index_container<connection,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::ordered_non_unique<mi::tag<tag_type>,
			mi::member<connection, connection_type, &connection::type>>,
		mi::hashed_non_unique<mi::tag<tag_address>,
			mi::member<connection, asio::ip::address, &connection::address>>,
		mi::hashed_non_unique<mi::tag<tag_subnetwork>,
			mi::member<connection, asio::ip::address, &connection::subnetwork>>
	>>;

	using ordered_attempts = boost::multi_index_container<attempt,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_non_unique<mi::tag<tag_address>,
			mi::member<attempt, asio::ip::address, &attempt::address>>
	>>;

	using ordered_cooldowns = boost::multi_index_container<cooldown,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_expiry>,
			mi::member<cooldown, std::chrono::steady_clock::time_point, &cooldown::expiry>>,
		mi::hashed_non_unique<mi::tag<tag_address>,
			mi::member<cooldown, asio::ip::address, &cooldown::address>>,
		mi::hashed_non_unique<mi::tag<tag_subnetwork>,
			mi::member<cooldown, asio::ip::address, &cooldown::subnetwork>>
	>>;
	// clang-format on

	ordered_connections connections; // Open connections in either direction
	ordered_attempts attempts; // Outbound connects in flight
	ordered_cooldowns cooldowns; // Closed connections still counted until their cooldown ends
};

std::string_view to_string (tcp_listener_index::result);
nano::stat::detail to_stat_detail (tcp_listener_index::result);
nano::stat::dir to_stat_dir (connection_type);
nano::transport::socket_endpoint to_socket_endpoint (connection_type);

class tcp_listener final
{
public:
	enum class accept_result
	{
		invalid,
		accepted,
		rejected,
		rejected_excluded,
		rejected_blocklisted,
		rejected_max_per_ip,
		rejected_max_per_subnetwork,
		rejected_max_inbound,
		rejected_max_outbound,
		error,
	};

public:
	tcp_listener (uint16_t port, tcp_config const &, nano::node &);
	~tcp_listener ();

	void start ();
	void stop ();

	/**
	 * @param port is optional, if 0 then default peering port is used
	 * @param callback is optional, invoked with the final outcome of the connection attempt
	 * @return true if connection attempt was initiated
	 */
	bool connect (
	asio::ip::address ip,
	uint16_t port = 0,
	nano::transport::connect_callback callback = noop<nano::transport::connect_callback> ());

	nano::tcp_endpoint endpoint () const;

	size_t connection_count () const;
	size_t connection_count (connection_type) const;
	size_t attempt_count () const;
	size_t realtime_count () const;
	size_t bootstrap_count () const;

	std::deque<std::shared_ptr<tcp_socket>> all_sockets () const;
	std::deque<std::shared_ptr<tcp_server>> all_servers () const;

	nano::container_info container_info () const;

public: // Events
	using connection_accepted_event_t = nano::observer_set<std::shared_ptr<tcp_socket>, std::shared_ptr<tcp_server>>;
	connection_accepted_event_t connection_accepted;

private: // Dependencies
	tcp_config const & config;
	nano::node & node;
	nano::stats & stats;
	nano::logger & logger;

private:
	asio::awaitable<void> start_impl ();
	asio::awaitable<void> run ();
	asio::awaitable<void> wait_available_slots () const;

	void run_cleanup ();
	void purge (nano::unique_lock<nano::mutex> &);
	void timeout ();

	asio::awaitable<void> connect_impl (asio::ip::tcp::endpoint, nano::transport::connect_callback callback);
	asio::awaitable<asio::ip::tcp::socket> connect_socket (asio::ip::tcp::endpoint);

	struct accept_return
	{
		accept_result result;
		std::shared_ptr<nano::transport::tcp_socket> socket;
		std::shared_ptr<nano::transport::tcp_server> server;
	};

	accept_return accept_one (asio::ip::tcp::socket, connection_type);
	accept_result check_limits (asio::ip::address const & ip, connection_type) const;
	asio::awaitable<asio::ip::tcp::socket> accept_socket ();

	// The index limits as the config and the node flags set them
	static tcp_listener_index::limits index_limits (tcp_config const &, nano::node const &);

private:
	uint16_t const port;

	tcp_listener_index index; // Connections, attempts and cooldowns with their limits, every access under `mutex`

	nano::async::strand strand;

	asio::ip::tcp::acceptor acceptor;
	asio::ip::tcp::endpoint local;

	std::atomic<bool> stopped;
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	nano::async::task task;
	std::thread cleanup_thread;
};

std::string_view to_string (tcp_listener::accept_result);
// Maps a limit verdict of the index to the listener's accept result
tcp_listener::accept_result to_accept_result (tcp_listener_index::result);
// Maps a connection rejection result to the corresponding error code
std::error_code to_error_code (tcp_listener::accept_result);
}
