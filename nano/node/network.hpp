#pragma once

#include <nano/lib/constants.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/interval.hpp>
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

	nano::endpoint endpoint () const;

	// Checks if we have enough channel capacity for the given traffic type
	// Returns true if at least half of target peers have spare capacity
	bool check_capacity (nano::transport::traffic_type, size_t target_count) const;
	// Computes target from fanout(scale)
	bool check_capacity_fanout (nano::transport::traffic_type, float scale = 1.0f) const;
	// Computes target as ratio of all peers (e.g., 0.5 = 50%)
	bool check_capacity_ratio (nano::transport::traffic_type, float ratio) const;

	size_t flood_message (nano::messages::message const &, nano::transport::traffic_type, float scale = 1.0f) const;
	size_t flood_message_all (nano::messages::message const &, nano::transport::traffic_type) const;

	size_t flood_keepalive (float scale = 1.0f) const;
	size_t flood_keepalive_self (float scale = 0.5f) const;

	size_t flood_vote (std::shared_ptr<nano::vote> const &, nano::transport::traffic_type, bool rebroadcasted = false) const;
	size_t flood_vote_all (std::shared_ptr<nano::vote> const &, nano::transport::traffic_type, bool rebroadcasted = false) const;
	size_t flood_vote_pr (std::shared_ptr<nano::vote> const &, nano::transport::traffic_type = nano::transport::traffic_type::vote) const;
	size_t flood_vote_non_pr (std::shared_ptr<nano::vote> const &, float scale, nano::transport::traffic_type = nano::transport::traffic_type::vote) const;

	// Flood block to a random selection of peers
	size_t flood_block (std::shared_ptr<nano::block> const &, nano::transport::traffic_type) const;
	size_t flood_block_all (std::shared_ptr<nano::block> const &, nano::transport::traffic_type) const;
	// Flood block to all PRs and a random selection of non-PRs
	size_t flood_block_initial (std::shared_ptr<nano::block> const &) const;
	void flood_block_many (std::deque<std::shared_ptr<nano::block>>, nano::transport::traffic_type, std::chrono::milliseconds delay = 10ms, std::function<void ()> callback = nullptr) const;

	void send_keepalive (std::shared_ptr<nano::transport::channel> const &) const;
	void send_keepalive_self (std::shared_ptr<nano::transport::channel> const &) const;

	// Trigger immediate reachout to preconfigured peers
	void trigger_reachout ();
	void reachout (std::string const & address, uint16_t port);
	void reachout_preconfigured ();

	void merge_peers (std::array<nano::endpoint, 8> const & ips);
	bool merge_peer (nano::endpoint const & ip);

	std::shared_ptr<nano::transport::channel> find_node_id (nano::account const &);
	std::shared_ptr<nano::transport::channel> find_channel (nano::endpoint const &);

	// Check if the endpoint address looks OK
	bool not_a_peer (nano::endpoint const &, bool allow_local_peers) const;
	// Should we reach out to this endpoint with a keepalive message? If yes, register a new reachout attempt
	bool track_reachout (nano::endpoint const &);

	using channel_filter = std::function<bool (std::shared_ptr<nano::transport::channel> const &)>;

	std::deque<std::shared_ptr<nano::transport::channel>> list (std::size_t max_count = 0, channel_filter = nullptr) const;
	std::deque<std::shared_ptr<nano::transport::channel>> list_non_pr (std::size_t max_count = 0, channel_filter = nullptr) const;

	std::deque<std::shared_ptr<nano::transport::channel>> list (std::size_t max_count, uint8_t minimum_version) const;
	std::deque<std::shared_ptr<nano::transport::channel>> list_non_pr (std::size_t max_count, uint8_t minimum_version) const;

	// Desired fanout for a given scale
	std::size_t fanout (float scale = 1.0f) const;

	void random_fill (std::array<nano::endpoint, 8> &) const;
	void fill_keepalive_self (std::array<nano::endpoint, 8> &) const;

	// Note: The minimum protocol version is used after the random selection, so number of peers can be less than expected.
	std::unordered_set<std::shared_ptr<nano::transport::channel>> random_set (std::size_t max_count, uint8_t minimum_version = 0) const;

	// Get the next peer for attempting a tcp bootstrap connection
	nano::tcp_endpoint bootstrap_peer ();
	void cleanup (std::chrono::steady_clock::time_point const & cutoff);
	std::size_t size () const;
	float size_log () const;
	bool empty () const;
	void erase (nano::transport::channel const &);
	/** Disconnects and adds peer to exclusion list */
	void exclude (std::shared_ptr<nano::transport::channel> const & channel);

	nano::container_info container_info () const;

public: // Handshake
	/** Verifies that handshake response matches our query. @returns true if OK */
	bool verify_handshake_response (nano::messages::node_id_handshake::response_payload const & response, nano::endpoint const & remote_endpoint);
	std::optional<nano::messages::node_id_handshake::query_payload> prepare_handshake_query (nano::endpoint const & remote_endpoint);
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
