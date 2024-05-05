#pragma once

#include <nano/lib/random.hpp>
#include <nano/node/common.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/tcp_channel.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <random>
#include <thread>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace nano::transport
{
class tcp_channels final
{
	friend class tcp_channel;
	friend class telemetry_simultaneous_requests_Test;
	friend class network_peer_max_tcp_attempts_subnetwork_Test;

public:
	explicit tcp_channels (nano::node &);
	~tcp_channels ();

	void start ();
	void stop ();

	std::shared_ptr<nano::transport::tcp_channel> create (std::shared_ptr<nano::transport::tcp_socket> const &, std::shared_ptr<nano::transport::tcp_server> const &, nano::account const & node_id);
	void erase (nano::tcp_endpoint const &);
	std::size_t size () const;
	std::shared_ptr<nano::transport::tcp_channel> find_channel (nano::tcp_endpoint const &) const;
	void random_fill (std::array<nano::endpoint, 8> &) const;
	std::unordered_set<std::shared_ptr<nano::transport::channel>> random_set (std::size_t, uint8_t = 0, bool = false) const;
	std::shared_ptr<nano::transport::tcp_channel> find_node_id (nano::account const &);
	// Get the next peer for attempting a tcp connection
	nano::tcp_endpoint bootstrap_peer ();
	bool max_ip_connections (nano::tcp_endpoint const & endpoint_a);
	bool max_subnetwork_connections (nano::tcp_endpoint const & endpoint_a);
	bool max_ip_or_subnetwork_connections (nano::tcp_endpoint const & endpoint_a);
	// Should we reach out to this endpoint with a keepalive message? If yes, register a new reachout attempt
	bool track_reachout (nano::endpoint const &);
	void purge (std::chrono::steady_clock::time_point cutoff_deadline);
	void list (std::deque<std::shared_ptr<nano::transport::channel>> &, uint8_t = 0, bool = true);
	void modify (std::shared_ptr<nano::transport::tcp_channel> const &, std::function<void (std::shared_ptr<nano::transport::tcp_channel> const &)>);
	void keepalive ();
	std::optional<nano::keepalive> sample_keepalive ();

	// Connection start
	void start_tcp (nano::endpoint const &);

	nano::container_info container_info () const;

private: // Dependencies
	nano::node & node;

private:
	void close ();
	bool check (nano::tcp_endpoint const &, nano::account const & node_id) const;

private:
	class channel_entry final
	{
	public:
		std::shared_ptr<nano::transport::tcp_channel> channel;
		std::shared_ptr<nano::transport::tcp_socket> socket;
		std::shared_ptr<nano::transport::tcp_server> response_server;

	public:
		channel_entry (std::shared_ptr<nano::transport::tcp_channel> channel_a, std::shared_ptr<nano::transport::tcp_socket> socket_a, std::shared_ptr<nano::transport::tcp_server> server_a) :
			channel (std::move (channel_a)), socket (std::move (socket_a)), response_server (std::move (server_a))
		{
		}
		nano::tcp_endpoint endpoint () const
		{
			return channel->get_tcp_endpoint ();
		}
		std::chrono::steady_clock::time_point last_bootstrap_attempt () const
		{
			return channel->get_last_bootstrap_attempt ();
		}
		boost::asio::ip::address ip_address () const
		{
			return nano::transport::ipv4_address_or_ipv6_subnet (endpoint ().address ());
		}
		boost::asio::ip::address subnetwork () const
		{
			return nano::transport::map_address_to_subnetwork (endpoint ().address ());
		}
		nano::account node_id () const
		{
			return channel->get_node_id ();
		}
		uint8_t network_version () const
		{
			return channel->get_network_version ();
		}
	};

	class attempt_entry final
	{
	public:
		nano::tcp_endpoint endpoint;
		boost::asio::ip::address address;
		boost::asio::ip::address subnetwork;
		std::chrono::steady_clock::time_point last_attempt{ std::chrono::steady_clock::now () };

	public:
		explicit attempt_entry (nano::tcp_endpoint const & endpoint_a) :
			endpoint (endpoint_a),
			address (nano::transport::ipv4_address_or_ipv6_subnet (endpoint_a.address ())),
			subnetwork (nano::transport::map_address_to_subnetwork (endpoint_a.address ()))
		{
		}
	};

	// clang-format off
	class endpoint_tag {};
	class ip_address_tag {};
	class subnetwork_tag {};
	class random_access_tag {};
	class last_bootstrap_attempt_tag {};
	class last_attempt_tag {};
	class node_id_tag {};
	class version_tag {};
	// clang-format on

	// clang-format off
	boost::multi_index_container<channel_entry,
	mi::indexed_by<
		mi::random_access<mi::tag<random_access_tag>>,
		mi::ordered_non_unique<mi::tag<last_bootstrap_attempt_tag>,
			mi::const_mem_fun<channel_entry, std::chrono::steady_clock::time_point, &channel_entry::last_bootstrap_attempt>>,
		mi::hashed_unique<mi::tag<endpoint_tag>,
			mi::const_mem_fun<channel_entry, nano::tcp_endpoint, &channel_entry::endpoint>>,
		mi::hashed_non_unique<mi::tag<node_id_tag>,
			mi::const_mem_fun<channel_entry, nano::account, &channel_entry::node_id>>,
		mi::ordered_non_unique<mi::tag<version_tag>,
			mi::const_mem_fun<channel_entry, uint8_t, &channel_entry::network_version>>,
		mi::hashed_non_unique<mi::tag<ip_address_tag>,
			mi::const_mem_fun<channel_entry, boost::asio::ip::address, &channel_entry::ip_address>>,
		mi::hashed_non_unique<mi::tag<subnetwork_tag>,
			mi::const_mem_fun<channel_entry, boost::asio::ip::address, &channel_entry::subnetwork>>>>
	channels;

	boost::multi_index_container<attempt_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<endpoint_tag>,
			mi::member<attempt_entry, nano::tcp_endpoint, &attempt_entry::endpoint>>,
		mi::hashed_non_unique<mi::tag<ip_address_tag>,
			mi::member<attempt_entry, boost::asio::ip::address, &attempt_entry::address>>,
		mi::hashed_non_unique<mi::tag<subnetwork_tag>,
			mi::member<attempt_entry, boost::asio::ip::address, &attempt_entry::subnetwork>>,
		mi::ordered_non_unique<mi::tag<last_attempt_tag>,
			mi::member<attempt_entry, std::chrono::steady_clock::time_point, &attempt_entry::last_attempt>>>>
	attempts;
	// clang-format on

private:
	std::atomic<bool> stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;

	mutable nano::random_generator rng;
};
}