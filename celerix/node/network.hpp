#pragma once

#include <celerix/lib/logging.hpp>
#include <celerix/lib/network_filter.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/node/messages.hpp>
#include <celerix/node/peer_exclusion.hpp>
#include <celerix/node/transport/common.hpp>
#include <celerix/node/transport/fwd.hpp>
#include <celerix/node/transport/tcp_channels.hpp>

#include <deque>
#include <memory>
#include <unordered_set>

namespace celerix
{
class node;

/**
 * Node ID cookies for node ID handshakes
 */
class syn_cookies final
{
public:
	syn_cookies (std::size_t max_peers_per_ip, celerix::logger &);

	void purge (std::chrono::steady_clock::time_point const &);
	// Returns boost::none if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	std::optional<celerix::uint256_union> assign (celerix::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate (celerix::endpoint const &, celerix::account const &, celerix::signature const &);
	/** Get cookie associated with endpoint and erases that cookie from this container */
	std::optional<celerix::uint256_union> cookie (celerix::endpoint const &);
	std::size_t cookies_size () const;
	celerix::container_info container_info () const;

private: // Dependencies
	celerix::logger & logger;

private:
	class syn_cookie_info final
	{
	public:
		celerix::uint256_union cookie;
		std::chrono::steady_clock::time_point created_at;
	};
	mutable celerix::mutex syn_cookie_mutex;
	std::unordered_map<celerix::endpoint, syn_cookie_info> cookies;
	std::unordered_map<boost::asio::ip::address, unsigned> cookies_per_ip;
	std::size_t max_cookies_per_ip;
};

class network_config final
{
public:
	explicit network_config (celerix::network_constants const & network)
	{
		if (network.is_dev_network () || network.is_beta_network ())
		{
			// During tests, all peers are on localhost
			max_peers_per_ip = 256;
			max_peers_per_subnetwork = 256;
		}
	}

	// TODO: Serialization & deserialization

public:
	std::chrono::milliseconds peer_reachout{ 250ms };
	std::chrono::milliseconds cached_peer_reachout{ 1s };

	/** Maximum number of peers per IP. It is also the max number of connections per IP */
	size_t max_peers_per_ip{ 4 };
	/** Maximum number of peers per subnetwork */
	size_t max_peers_per_subnetwork{ 16 };

	size_t duplicate_filter_size{ 1024 * 1024 };
	uint64_t duplicate_filter_cutoff{ 60 };
};

class network final
{
public:
	network (celerix::node &, uint16_t port);
	~network ();

	void start ();
	void stop ();

	celerix::endpoint endpoint () const;

	void flood_message (celerix::message const &, celerix::transport::traffic_type, float scale = 1.0f) const;
	void flood_keepalive (float scale = 1.0f) const;
	void flood_keepalive_self (float scale = 0.5f) const;
	void flood_vote (std::shared_ptr<celerix::vote> const &, float scale, bool rebroadcasted = false) const;
	void flood_vote_pr (std::shared_ptr<celerix::vote> const &, bool rebroadcasted = false) const;
	void flood_vote_non_pr (std::shared_ptr<celerix::vote> const &, float scale, bool rebroadcasted = false) const;
	// Flood block to all PRs and a random selection of non-PRs
	void flood_block_initial (std::shared_ptr<celerix::block> const &) const;
	// Flood block to a random selection of peers
	void flood_block (std::shared_ptr<celerix::block> const &, celerix::transport::traffic_type) const;
	void flood_block_many (std::deque<std::shared_ptr<celerix::block>>, celerix::transport::traffic_type, std::chrono::milliseconds delay = 10ms, std::function<void ()> callback = nullptr) const;

	void send_keepalive (std::shared_ptr<celerix::transport::channel> const &) const;
	void send_keepalive_self (std::shared_ptr<celerix::transport::channel> const &) const;

	void merge_peers (std::array<celerix::endpoint, 8> const & ips);
	bool merge_peer (celerix::endpoint const & ip);

	std::shared_ptr<celerix::transport::channel> find_node_id (celerix::account const &);
	std::shared_ptr<celerix::transport::channel> find_channel (celerix::endpoint const &);

	// Check if the endpoint address looks OK
	bool not_a_peer (celerix::endpoint const &, bool allow_local_peers) const;
	// Should we reach out to this endpoint with a keepalive message? If yes, register a new reachout attempt
	bool track_reachout (celerix::endpoint const &);

	std::deque<std::shared_ptr<celerix::transport::channel>> list (std::size_t max_count = 0, uint8_t minimum_version = 0) const;
	std::deque<std::shared_ptr<celerix::transport::channel>> list_non_pr (std::size_t max_count, uint8_t minimum_version = 0) const;

	// Desired fanout for a given scale
	std::size_t fanout (float scale = 1.0f) const;

	void random_fill (std::array<celerix::endpoint, 8> &) const;
	void fill_keepalive_self (std::array<celerix::endpoint, 8> &) const;

	// Note: The minimum protocol version is used after the random selection, so number of peers can be less than expected.
	std::unordered_set<std::shared_ptr<celerix::transport::channel>> random_set (std::size_t max_count, uint8_t minimum_version = 0) const;

	// Get the next peer for attempting a tcp bootstrap connection
	celerix::tcp_endpoint bootstrap_peer ();
	void cleanup (std::chrono::steady_clock::time_point const & cutoff);
	std::size_t size () const;
	float size_sqrt () const;
	bool empty () const;
	void erase (celerix::transport::channel const &);
	/** Disconnects and adds peer to exclusion list */
	void exclude (std::shared_ptr<celerix::transport::channel> const & channel);

	celerix::container_info container_info () const;

public: // Handshake
	/** Verifies that handshake response matches our query. @returns true if OK */
	bool verify_handshake_response (celerix::node_id_handshake::response_payload const & response, celerix::endpoint const & remote_endpoint);
	std::optional<celerix::node_id_handshake::query_payload> prepare_handshake_query (celerix::endpoint const & remote_endpoint);
	celerix::node_id_handshake::response_payload prepare_handshake_response (celerix::node_id_handshake::query_payload const & query, bool v2) const;

private:
	void run_cleanup ();
	void run_keepalive ();
	void run_reachout ();
	void run_reachout_cached ();

private: // Dependencies
	network_config const & config;
	celerix::node & node;

public:
	celerix::networks const id;
	celerix::syn_cookies syn_cookies;
	boost::asio::ip::tcp::resolver resolver;
	celerix::peer_exclusion excluded_peers;
	celerix::network_filter filter;
	celerix::transport::tcp_channels tcp_channels;
	std::atomic<uint16_t> port{ 0 };

public: // Callbacks
	std::function<void ()> disconnect_observer{ [] () {} };

private:
	std::atomic<bool> stopped{ false };
	mutable celerix::mutex mutex;
	celerix::condition_variable condition;
	std::thread cleanup_thread;
	std::thread keepalive_thread;
	std::thread reachout_thread;
	std::thread reachout_cached_thread;

public:
	static std::size_t const buffer_size = 512;

	static std::size_t confirm_req_hashes_max;
	static std::size_t confirm_ack_hashes_max;
};
}
