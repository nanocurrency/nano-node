#pragma once

#include <nano/lib/logging.hpp>
#include <nano/node/common.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/peer_exclusion.hpp>
#include <nano/node/transport/common.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/node/transport/tcp_channels.hpp>
#include <nano/secure/network_filter.hpp>

#include <deque>
#include <memory>
#include <unordered_set>

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
	// Returns boost::none if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	std::optional<nano::uint256_union> assign (nano::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate (nano::endpoint const &, nano::account const &, nano::signature const &);
	/** Get cookie associated with endpoint and erases that cookie from this container */
	std::optional<nano::uint256_union> cookie (nano::endpoint const &);

	std::unique_ptr<container_info_component> collect_container_info (std::string const &);
	std::size_t cookies_size ();

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

	// TODO: Serialization & deserialization

public:
	std::chrono::milliseconds peer_reachout{ 250ms };
	std::chrono::milliseconds cached_peer_reachout{ 1s };

	/** Maximum number of peers per IP. It is also the max number of connections per IP */
	size_t max_peers_per_ip{ 4 };
	/** Maximum number of peers per subnetwork */
	size_t max_peers_per_subnetwork{ 16 };
};

class network final
{
public:
	network (nano::node &, uint16_t port);
	~network ();

	void start ();
	void stop ();

	void flood_message (nano::message &, nano::transport::buffer_drop_policy const = nano::transport::buffer_drop_policy::limiter, float const = 1.0f);
	void flood_keepalive (float const scale_a = 1.0f);
	void flood_keepalive_self (float const scale_a = 0.5f);
	void flood_vote (std::shared_ptr<nano::vote> const &, float scale, bool rebroadcasted = false);
	void flood_vote_pr (std::shared_ptr<nano::vote> const &, bool rebroadcasted = false);
	// Flood block to all PRs and a random selection of non-PRs
	void flood_block_initial (std::shared_ptr<nano::block> const &);
	// Flood block to a random selection of peers
	void flood_block (std::shared_ptr<nano::block> const &, nano::transport::buffer_drop_policy const = nano::transport::buffer_drop_policy::limiter);
	void flood_block_many (std::deque<std::shared_ptr<nano::block>>, std::function<void ()> = nullptr, unsigned = broadcast_interval_ms);
	void merge_peers (std::array<nano::endpoint, 8> const &);
	void merge_peer (nano::endpoint const &);
	void send_keepalive (std::shared_ptr<nano::transport::channel> const &);
	void send_keepalive_self (std::shared_ptr<nano::transport::channel> const &);
	std::shared_ptr<nano::transport::channel> find_node_id (nano::account const &);
	std::shared_ptr<nano::transport::channel> find_channel (nano::endpoint const &);
	bool not_a_peer (nano::endpoint const &, bool allow_local_peers);
	// Should we reach out to this endpoint with a keepalive message? If yes, register a new reachout attempt
	bool track_reachout (nano::endpoint const &);
	std::deque<std::shared_ptr<nano::transport::channel>> list (std::size_t max_count = 0, uint8_t = 0, bool = true);
	std::deque<std::shared_ptr<nano::transport::channel>> list_non_pr (std::size_t);
	// Desired fanout for a given scale
	std::size_t fanout (float scale = 1.0f) const;
	void random_fill (std::array<nano::endpoint, 8> &) const;
	void fill_keepalive_self (std::array<nano::endpoint, 8> &) const;
	// Note: The minimum protocol version is used after the random selection, so number of peers can be less than expected.
	std::unordered_set<std::shared_ptr<nano::transport::channel>> random_set (std::size_t count, uint8_t min_version = 0, bool include_temporary_channels = false) const;
	// Get the next peer for attempting a tcp bootstrap connection
	nano::tcp_endpoint bootstrap_peer ();
	nano::endpoint endpoint () const;
	void cleanup (std::chrono::steady_clock::time_point const & cutoff);
	std::size_t size () const;
	float size_sqrt () const;
	bool empty () const;
	void erase (nano::transport::channel const &);
	/** Disconnects and adds peer to exclusion list */
	void exclude (std::shared_ptr<nano::transport::channel> const & channel);
	void inbound (nano::message const &, std::shared_ptr<nano::transport::channel> const &);

public: // Handshake
	/** Verifies that handshake response matches our query. @returns true if OK */
	bool verify_handshake_response (nano::node_id_handshake::response_payload const & response, nano::endpoint const & remote_endpoint);
	std::optional<nano::node_id_handshake::query_payload> prepare_handshake_query (nano::endpoint const & remote_endpoint);
	nano::node_id_handshake::response_payload prepare_handshake_response (nano::node_id_handshake::query_payload const & query, bool v2) const;

private:
	void run_cleanup ();
	void run_keepalive ();
	void run_reachout ();
	void run_reachout_cached ();

private: // Dependencies
	network_config const & config;
	nano::node & node;

public:
	nano::networks const id;
	nano::syn_cookies syn_cookies;
	boost::asio::ip::tcp::resolver resolver;
	nano::peer_exclusion excluded_peers;
	nano::network_filter publish_filter;
	nano::transport::tcp_channels tcp_channels;
	std::atomic<uint16_t> port{ 0 };

public: // Callbacks
	std::function<void ()> disconnect_observer{ [] () {} };
	// Called when a new channel is observed
	std::function<void (std::shared_ptr<nano::transport::channel>)> channel_observer{ [] (auto) {} };

private:
	std::atomic<bool> stopped{ false };
	mutable nano::mutex mutex;
	nano::condition_variable condition;
	std::thread cleanup_thread;
	std::thread keepalive_thread;
	std::thread reachout_thread;
	std::thread reachout_cached_thread;

public:
	static unsigned const broadcast_interval_ms = 10;
	static std::size_t const buffer_size = 512;

	static std::size_t const confirm_req_hashes_max = 7;
	static std::size_t const confirm_ack_hashes_max = 12;
};

std::unique_ptr<container_info_component> collect_container_info (network & network, std::string const & name);
}
