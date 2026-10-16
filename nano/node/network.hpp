#pragma once

#include <nano/lib/constants.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/network_filter.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/peer_exclusion.hpp>
#include <nano/node/transport/common.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/node/transport/tcp_channels.hpp>

#include <chrono>
#include <deque>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace std::chrono_literals;
namespace nano
{
class node;

/**
 * Node ID cookies for node ID handshakes
 */
class syn_cookies final
{
public:
	syn_cookies (std::size_t max_peers_per_ip, nano::logger &);

	void purge (std::chrono::steady_clock::time_point const &);
	// Returns std::nullopt if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	std::optional<nano::uint256_union> assign (nano::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate (nano::endpoint const &, nano::account const &, nano::signature const &);
	/** Get cookie associated with endpoint and erases that cookie from this container */
	std::optional<nano::uint256_union> cookie (nano::endpoint const &);
	std::size_t cookies_size () const;
	nano::container_info container_info () const;

private: // Dependencies
	nano::logger & logger;

private:
	class syn_cookie_info final
	{
	public:
		nano::uint256_union cookie;
		std::chrono::steady_clock::time_point created_at;
	};
	mutable nano::mutex syn_cookie_mutex;
	std::unordered_map<nano::endpoint, syn_cookie_info> cookies;
	std::unordered_map<boost::asio::ip::address, unsigned> cookies_per_ip;
	std::size_t max_cookies_per_ip;
};

class network_config final
{
public:
	explicit network_config (nano::network_constants const & network)
	{
		if (network.is_dev_network () || network.is_beta_network ())
		{
			// During tests, all peers are on localhost
			max_peers_per_ip = 256;
			max_peers_per_subnetwork = 256;
		}
	}

	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	std::chrono::milliseconds peer_reachout{ 250ms };
	std::chrono::milliseconds cached_peer_reachout{ 1s };

	/** Maximum number of peers per IP. It is also the max number of connections per IP */
	size_t max_peers_per_ip{ 4 };
	/** Maximum number of peers per subnetwork */
	size_t max_peers_per_subnetwork{ 16 };

	size_t duplicate_filter_size{ 1024 * 1024 };
	uint64_t duplicate_filter_cutoff{ 60 };

	size_t minimum_fanout{ 2 };
};

class network final
{
public:
	network (nano::node &, uint16_t port);
	~network ();

	void start ();
	void stop ();

	// This node's own endpoint, loopback with the listening port
	nano::endpoint endpoint () const;

	// Checks if we have enough channel capacity for the given traffic type
	// Returns true if at least half of target peers have spare capacity
	bool check_capacity (nano::transport::traffic_type, size_t target_count) const;
	// Computes target from fanout(scale)
	bool check_capacity_fanout (nano::transport::traffic_type, float scale = 1.0f) const;
	// Computes target as ratio of all peers (e.g., 0.5 = 50%)
	bool check_capacity_ratio (nano::transport::traffic_type, float ratio) const;

	// Floods the message to a fanout of the channels with room for the traffic type, scaled by `scale`, and returns how many queued it
	size_t flood_message (nano::messages::message const &, nano::transport::traffic_type, float scale = 1.0f) const;
	// Floods the message to every channel with room for the traffic type and returns how many queued it
	size_t flood_message_all (nano::messages::message const &, nano::transport::traffic_type) const;

	// Sends one message to every channel of the range, serialized once for all of them
	template <std::ranges::input_range Channels>
	size_t send_to (nano::messages::message const &, nano::transport::traffic_type, Channels const &) const;
	// Sends an already serialized message of the given type to every channel of the range
	template <std::ranges::input_range Channels>
	size_t send_to (nano::shared_const_buffer const &, nano::messages::message_type, nano::transport::traffic_type, Channels const &) const;

	// Floods a keepalive carrying random peers to a scaled fanout
	size_t flood_keepalive (float scale = 1.0f) const;
	// Floods a keepalive carrying this node's own endpoints to a scaled fanout
	size_t flood_keepalive_self (float scale = 0.5f) const;

	// Floods a vote to a fanout, marked as a rebroadcast when it is one
	size_t flood_vote (std::shared_ptr<nano::vote> const &, nano::transport::traffic_type, bool rebroadcasted = false) const;
	// Floods a vote to every channel, marked as a rebroadcast when it is one
	size_t flood_vote_all (std::shared_ptr<nano::vote> const &, nano::transport::traffic_type, bool rebroadcasted = false) const;
	// Sends a vote to every principal representative, whether or not its channel has room
	size_t flood_vote_pr (std::shared_ptr<nano::vote> const &, nano::transport::traffic_type = nano::transport::traffic_type::vote) const;
	// Floods a vote to a scaled fanout of the channels that are not principal representatives
	size_t flood_vote_non_pr (std::shared_ptr<nano::vote> const &, float scale, nano::transport::traffic_type = nano::transport::traffic_type::vote) const;

	// Floods a block to a fanout
	size_t flood_block (std::shared_ptr<nano::block> const &, nano::transport::traffic_type) const;
	// Floods a block to every channel
	size_t flood_block_all (std::shared_ptr<nano::block> const &, nano::transport::traffic_type) const;
	// Sends a block this node made to every principal representative and floods it to a fanout of the others
	size_t flood_block_initial (std::shared_ptr<nano::block> const &) const;
	// Floods the blocks one at a time, `delay` apart, and calls `callback` after the last
	void flood_block_many (std::deque<std::shared_ptr<nano::block>>, nano::transport::traffic_type, std::chrono::milliseconds delay = 10ms, std::function<void ()> callback = nullptr) const;

	// Sends the channel a keepalive carrying random peers
	void send_keepalive (std::shared_ptr<nano::transport::channel> const &) const;
	// Sends the channel a keepalive carrying this node's own endpoints
	void send_keepalive_self (std::shared_ptr<nano::transport::channel> const &) const;

	// Resolves the address and connects to each of its endpoints that has no channel yet
	void reachout (std::string const & address, uint16_t port, nano::transport::connect_callback callback = noop<nano::transport::connect_callback> ());
	// Reaches out to every preconfigured peer
	void reachout_preconfigured ();
	// Makes the run loop reach out to the preconfigured peers now instead of at the next interval
	void trigger_reachout ();

	// Connects to each endpoint of a keepalive that is worth reaching out to
	void merge_peers (std::array<nano::endpoint, 8> const & ips);
	// Connects to the endpoint if it is worth reaching out to, true when an attempt was started
	bool merge_peer (nano::endpoint const & ip);

	// The channel of the peer with this node id, if any
	std::shared_ptr<nano::transport::channel> find_node_id (nano::account const &);
	// The channel to this endpoint, if any
	std::shared_ptr<nano::transport::channel> find_channel (nano::endpoint const &);

	// Whether the endpoint is one not to connect to: unspecified, reserved, this node itself, or off loopback when peers must be local
	bool not_a_peer (nano::endpoint const &) const;
	// Should we reach out to this endpoint with a keepalive message? If yes, register a new reachout attempt
	bool track_reachout (nano::endpoint const &);

	using channel_filter = std::function<bool (std::shared_ptr<nano::transport::channel> const &)>;

	// All channels passing the filter in random order, cut to `max_count` unless it is 0
	std::deque<std::shared_ptr<nano::transport::channel>> list (std::size_t max_count = 0, channel_filter = nullptr) const;
	// All channels passing the filter in random order, without principal representatives and cut to `max_count`
	std::deque<std::shared_ptr<nano::transport::channel>> list_non_pr (std::size_t max_count = 0, channel_filter = nullptr) const;

	// All channels on at least `minimum_version` of the protocol in random order, cut to `max_count` unless it is 0
	std::deque<std::shared_ptr<nano::transport::channel>> list (std::size_t max_count, uint8_t minimum_version) const;
	// All channels on at least `minimum_version` of the protocol in random order, without principal representatives and cut to `max_count`
	std::deque<std::shared_ptr<nano::transport::channel>> list_non_pr (std::size_t max_count, uint8_t minimum_version) const;

	// Up to `count` channels picked uniformly at random among those passing the filter, in random order
	nano::transport::channel_list sample (std::size_t count, channel_filter = nullptr) const;
	// Up to `count` channels picked uniformly at random among those passing the filter, without principal representatives and possibly fewer even when more qualify
	nano::transport::channel_list sample_non_pr (std::size_t count, channel_filter = nullptr) const;

	// Desired fanout for a given scale
	std::size_t fanout (float scale = 1.0f) const;

	// Fills the slots with the peering endpoints of random peers, unspecified where there are fewer
	void random_fill (std::array<nano::endpoint, 8> &) const;
	// Fills the slots like `random_fill`, then puts this node's own listening and external endpoints in the first two
	void fill_keepalive_self (std::array<nano::endpoint, 8> &) const;

	// Up to `max_count` distinct channels drawn at random, keeping only those on at least `minimum_version`
	// The draws are capped at twice the count and the version is checked after each draw, so the set can fall short even when enough channels qualify
	std::unordered_set<std::shared_ptr<nano::transport::channel>> random_set (std::size_t max_count, uint8_t minimum_version = 0) const;

	// The next peer to try a bootstrap connection to
	nano::tcp_endpoint bootstrap_peer ();
	// Drops the channels idle since `cutoff` and fires the disconnect observer when none is left
	void cleanup (std::chrono::steady_clock::time_point const & cutoff);
	// The number of connected peers, one channel each
	std::size_t size () const;
	// Natural log of the peer count, the base of the fanout
	float size_log () const;
	// Whether no peer is connected
	bool empty () const;
	// Removes a TCP channel from the container by its remote endpoint
	void erase (nano::transport::channel const &);
	// Adds the channel's peer to the exclusion list and removes the channel
	void exclude (std::shared_ptr<nano::transport::channel> const & channel);

	nano::container_info container_info () const;

public: // Handshake
	// Whether the response answers the cookie sent to the endpoint with a valid signature, and is not this node or another genesis
	bool verify_handshake_response (nano::messages::node_id_handshake::response_payload const & response, nano::endpoint const & remote_endpoint);
	// A handshake query with a fresh cookie for the endpoint, none when it already has one or its IP is at the cap
	std::optional<nano::messages::node_id_handshake::query_payload> prepare_handshake_query (nano::endpoint const & remote_endpoint);
	// A handshake response with this node's id and whatever the version carries of salt, genesis and capabilities
	nano::messages::node_id_handshake::response_payload prepare_handshake_response (nano::messages::node_id_handshake::query_payload const & query, nano::messages::handshake_version version) const;

private:
	void run_cleanup ();
	void run_keepalive ();
	void run_reachout ();
	void run_reachout_cached ();
	void run_reachout_preconfigured ();

private: // Dependencies
	network_config const & config;
	nano::node & node;

public:
	nano::network_type const id;
	nano::syn_cookies syn_cookies;
	boost::asio::ip::tcp::resolver resolver;
	nano::peer_exclusion excluded_peers;
	nano::network_filter filter;
	nano::transport::tcp_channels tcp_channels;
	std::atomic<uint16_t> port{ 0 };

public: // Callbacks
	std::function<void ()> disconnect_observer{ [] () {} };

private:
	nano::interval_mt reachout_preconfigured_interval;

	// Rate limits per-peer logs from preconfigured reachout attempts
	struct reachout_preconfigured_log
	{
		std::chrono::steady_clock::time_point last_success{};
		std::chrono::steady_clock::time_point last_failure{};

		bool should_log (bool success);
	};
	nano::locked<std::unordered_map<std::string, reachout_preconfigured_log>> reachout_preconfigured_logs;

private:
	std::atomic<bool> stopped{ false };
	mutable nano::mutex mutex;
	nano::condition_variable condition;
	std::thread cleanup_thread;
	std::thread keepalive_thread;
	std::thread reachout_thread;
	std::thread reachout_cached_thread;
	std::thread reachout_preconfigured_thread;

public:
	static std::size_t const buffer_size = 512;

	static std::size_t confirm_req_hashes_max;
	static std::size_t confirm_ack_hashes_max;
};
}

template <std::ranges::input_range Channels>
size_t nano::network::send_to (nano::messages::message const & message, nano::transport::traffic_type type, Channels const & channels) const
{
	if (std::ranges::empty (channels))
	{
		return 0;
	}
	return send_to (message.to_shared_const_buffer (), message.type (), type, channels);
}

template <std::ranges::input_range Channels>
size_t nano::network::send_to (nano::shared_const_buffer const & buffer, nano::messages::message_type message_type, nano::transport::traffic_type type, Channels const & channels) const
{
	size_t result = 0;
	for (auto const & channel : channels)
	{
		result += channel->send_buffer (buffer, message_type, type);
	}
	return result;
}
