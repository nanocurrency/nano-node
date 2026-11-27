#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/random.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/endpoint_templ.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/common.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/node/transport/reachout_index.hpp>
#include <nano/node/transport/tcp_channel.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <memory>
#include <random>
#include <system_error>
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
		duplicate, // A live channel to the same node from the same IP exists
		occupied, // The endpoint, or a dead channel to the node waiting for purge, is still registered
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

	// Admits a channel for a socket whose handshake completed unless the peer is invalid, blocklisted, already connected from the same IP or its endpoint is already registered
	create_result create (std::shared_ptr<nano::transport::tcp_socket> const &, std::shared_ptr<nano::transport::tcp_server> const &, nano::transport::peer_info const &);

	// Closes and removes the channel at the endpoint, with its socket and server
	void erase (nano::tcp_endpoint const &);
	std::size_t size () const;
	// The channel to the remote endpoint, or null
	std::shared_ptr<nano::transport::tcp_channel> find_channel (nano::tcp_endpoint const &) const;
	// A channel to the node id, or null
	std::shared_ptr<nano::transport::tcp_channel> find_node_id (nano::account const &);
	// Records a reachout to the endpoint and hands out the handle its connection attempt reports through; null when the endpoint has a channel, an earlier reachout still holds it, or its IP or subnetwork is at the peer limit in channels or in reachouts
	std::shared_ptr<nano::transport::reachout> track_reachout (nano::endpoint const &);
	// Closes blocklisted and outdated channels and those that have sent nothing since the cutoff, forgets old reachouts and removes dead channels
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

	// Starts an outbound connection to the endpoint, the callback receives its outcome
	void start_tcp (
	nano::endpoint const & endpoint,
	nano::transport::connect_callback callback = nullptr);

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

	// clang-format off
	class endpoint_tag {};
	class ip_address_tag {};
	class subnetwork_tag {};
	class random_access_tag {};
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
	// clang-format on

	// Reachouts holding their endpoints back, every access under `mutex`
	nano::transport::reachout_index reachouts;

private:
	std::atomic<bool> stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;

	mutable nano::random_generator rng;
};

// Maps a channel admission verdict to the error a handshake ends with, empty when accepted
std::error_code to_error_code (tcp_channels::channel_result);
}
