#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/random.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/endpoint_templ.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/common.hpp>
#include <nano/node/transport/fwd.hpp>
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

public:
	// Outcome of checking a new channel against the container
	enum class channel_result
	{
		accepted,
		rejected,
		duplicate
	};

public:
	explicit tcp_channels (nano::node &);
	~tcp_channels ();

	void start ();
	void stop ();

	// Outcome of `create`, with the channel when it was accepted
	struct create_result
	{
		channel_result result;
		std::shared_ptr<nano::transport::tcp_channel> channel;
	};

	// Admits a channel for a socket whose handshake completed unless the peer is invalid, blocklisted or already connected from the same IP, and clears its reachout attempt
	create_result create (std::shared_ptr<nano::transport::tcp_socket> const &, std::shared_ptr<nano::transport::tcp_server> const &, nano::transport::peer_info const &);

	// Closes and removes the channel at the endpoint, with its socket and server
	void erase (nano::tcp_endpoint const &);
	std::size_t size () const;
	// The channel to the remote endpoint, or null
	std::shared_ptr<nano::transport::tcp_channel> find_channel (nano::tcp_endpoint const &) const;
	// A channel to the node id, or null
	std::shared_ptr<nano::transport::tcp_channel> find_node_id (nano::account const &);
	// Whether the channels or the reachout attempts at the endpoint's address have reached the per IP limit
	bool max_ip_connections (nano::tcp_endpoint const & endpoint);
	// Whether the channels or the reachout attempts in the endpoint's subnetwork have reached the per subnetwork limit
	bool max_subnetwork_connections (nano::tcp_endpoint const & endpoint);
	// Whether either of the two limits above is reached
	bool max_ip_or_subnetwork_connections (nano::tcp_endpoint const & endpoint);
	// Should we reach out to this endpoint with a keepalive message? If yes, register a new reachout attempt
	bool track_reachout (nano::endpoint const &);
	// Closes blocklisted and outdated channels and those that have sent nothing since the cutoff, drops older reachout attempts and removes dead channels
	void purge (std::chrono::steady_clock::time_point cutoff_deadline);

	// Selects channels for `list` and `sample`
	using channel_filter = std::function<bool (std::shared_ptr<nano::transport::channel> const &)>;
	// Channels passing the filter, in connection order
	std::deque<std::shared_ptr<nano::transport::channel>> list (channel_filter) const;
	// Channels on at least the protocol version, in connection order
	std::deque<std::shared_ptr<nano::transport::channel>> list (uint8_t minimum_version = 0) const;
	// Up to `max_count` live channels on at least the version, drawn at random with at most twice as many draws
	std::unordered_set<std::shared_ptr<nano::transport::channel>> random_set (std::size_t max_count, uint8_t minimum_version = 0) const;

	// Up to `count` channels picked uniformly at random among those passing the filter, in random order
	nano::transport::channel_list sample (std::size_t count, channel_filter const & filter = nullptr) const;

	// Sends a keepalive to every channel that has sent nothing since the cutoff
	void keepalive (std::chrono::steady_clock::time_point cutoff_deadline);
	// Takes the last keepalive received on a randomly picked channel, or nothing when the picks find none
	std::optional<nano::messages::keepalive> sample_keepalive ();

	// Connection start
	bool start_tcp (
	nano::endpoint const & endpoint,
	nano::transport::connect_callback callback = noop<nano::transport::connect_callback> ());

	std::deque<std::shared_ptr<tcp_socket>> all_sockets () const;
	std::deque<std::shared_ptr<tcp_server>> all_servers () const;
	std::deque<std::shared_ptr<tcp_channel>> all_channels () const;

	nano::container_info container_info () const;

private: // Dependencies
	nano::node & node;

private:
	// Closes and removes every channel
	void close ();
	// Whether a channel to the endpoint and node id may be admitted, called with the mutex held
	channel_result check (nano::tcp_endpoint const &, nano::account const & node_id) const;

private:
	// A channel with the socket and server it runs on
	class channel_entry final
	{
	public:
		std::shared_ptr<tcp_channel> channel;
		std::shared_ptr<tcp_socket> socket;
		std::shared_ptr<tcp_server> server;

	public:
		channel_entry (std::shared_ptr<tcp_channel> channel_a, std::shared_ptr<tcp_socket> socket_a, std::shared_ptr<tcp_server> server_a) :
			channel (std::move (channel_a)),
			socket (std::move (socket_a)),
			server (std::move (server_a))
		{
			release_assert (socket);
			release_assert (server);
			release_assert (channel);
		}
		nano::tcp_endpoint endpoint () const
		{
			return channel->get_remote_endpoint ();
		}
		// The IPv4 address or IPv6 subnet the per IP limit counts by
		boost::asio::ip::address ip_address () const
		{
			return nano::transport::ipv4_address_or_ipv6_subnet (endpoint ().address ());
		}
		// The subnetwork the per subnetwork limit counts by
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

	// A reachout attempt, kept until a channel to the endpoint is created or a purge drops it
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
	class last_attempt_tag {};
	class node_id_tag {};
	class version_tag {};
	// clang-format on

	// Channels in connection order, looked up by endpoint, node id, version, address and subnetwork
	// clang-format off
	boost::multi_index_container<channel_entry,
	mi::indexed_by<
		mi::random_access<mi::tag<random_access_tag>>,
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

	// Reachout attempts, looked up by endpoint, address and subnetwork and ordered by age
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
