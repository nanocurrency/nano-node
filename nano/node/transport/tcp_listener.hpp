#pragma once

#include <nano/boost/asio/strand.hpp>
#include <nano/node/common.hpp>

#include <atomic>

namespace nano::transport
{
class socket;
class tcp_server;

/**
 * Server side portion of bootstrap sessions. Listens for new socket connections and spawns tcp_server objects when connected.
 */
class tcp_listener final : public std::enable_shared_from_this<tcp_listener>
{
public:
	tcp_listener (uint16_t port, nano::node &, std::size_t max_inbound_connections);
	~tcp_listener ();

	void start (std::function<bool (std::shared_ptr<nano::transport::socket> const &, boost::system::error_code const &)> callback);
	void stop ();

	void accept_action (boost::system::error_code const &, std::shared_ptr<nano::transport::socket> const &);

	std::size_t connection_count ();
	nano::tcp_endpoint endpoint () const;

	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);

private: // Dependencies
	nano::node & node;

private:
	void on_connection (std::function<bool (std::shared_ptr<nano::transport::socket> const &, boost::system::error_code const &)> callback_a);
	void evict_dead_connections ();
	void on_connection_requeue_delayed (std::function<bool (std::shared_ptr<nano::transport::socket> const & new_connection, boost::system::error_code const &)>);
	/** Checks whether the maximum number of connections per IP was reached. If so, it returns true. */
	bool limit_reached_for_incoming_ip_connections (std::shared_ptr<nano::transport::socket> const & new_connection);
	bool limit_reached_for_incoming_subnetwork_connections (std::shared_ptr<nano::transport::socket> const & new_connection);
	void cleanup ();

public:
	std::atomic<std::size_t> bootstrap_count{ 0 };
	std::atomic<std::size_t> realtime_count{ 0 };

private:
	std::unordered_map<nano::transport::tcp_server *, std::weak_ptr<nano::transport::tcp_server>> connections;
	std::multimap<boost::asio::ip::address, std::weak_ptr<socket>> connections_per_address;

	boost::asio::strand<boost::asio::io_context::executor_type> strand;
	boost::asio::ip::tcp::acceptor acceptor;
	boost::asio::ip::tcp::endpoint local;
	std::size_t const max_inbound_connections;

	std::atomic<bool> stopped;
	mutable nano::mutex mutex;
};
}