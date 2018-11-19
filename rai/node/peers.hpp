#pragma once

#include <boost/asio/ip/address.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>
#include <chrono>
#include <deque>
#include <mutex>
#include <rai/lib/numbers.hpp>
#include <rai/node/common.hpp>
#include <unordered_set>
#include <vector>

namespace rai
{
rai::endpoint map_endpoint_to_v6 (rai::endpoint const &);

/** Multi-index helper */
class peer_by_ip_addr
{
};

/** Multi-index helper */
class peer_attempt
{
public:
	rai::endpoint endpoint;
	std::chrono::steady_clock::time_point last_attempt;
};

/** Node handshake cookie */
class syn_cookie_info
{
public:
	rai::uint256_union cookie;
	std::chrono::steady_clock::time_point created_at;
};

/** Collects peer contact information */
class peer_information
{
public:
	peer_information (rai::endpoint const &, unsigned);
	peer_information (rai::endpoint const &, std::chrono::steady_clock::time_point const &, std::chrono::steady_clock::time_point const &);
	rai::endpoint endpoint;
	boost::asio::ip::address ip_address;
	std::chrono::steady_clock::time_point last_contact;
	std::chrono::steady_clock::time_point last_attempt;
	std::chrono::steady_clock::time_point last_bootstrap_attempt;
	std::chrono::steady_clock::time_point last_rep_request;
	std::chrono::steady_clock::time_point last_rep_response;
	rai::amount rep_weight;
	rai::account probable_rep_account;
	unsigned network_version;
	boost::optional<rai::account> node_id;
};

/** Manages a set of disovered peers */
class peer_container
{
public:
	peer_container (rai::endpoint const &);
	// We were contacted by endpoint, update peers
	// Returns true if a Node ID handshake should begin
	bool contacted (rai::endpoint const &, unsigned);
	// Unassigned, reserved, self
	bool not_a_peer (rai::endpoint const &, bool);
	// Returns true if peer was already known
	bool known_peer (rai::endpoint const &);
	// Notify of peer we received from
	bool insert (rai::endpoint const &, unsigned);
	std::unordered_set<rai::endpoint> random_set (size_t);
	void random_fill (std::array<rai::endpoint, 8> &);
	// Request a list of the top known representatives
	std::vector<peer_information> representatives (size_t);
	// List of all peers
	std::deque<rai::endpoint> list ();
	std::map<rai::endpoint, unsigned> list_version ();
	std::vector<peer_information> list_vector (size_t);
	// A list of random peers sized for the configured rebroadcast fanout
	std::deque<rai::endpoint> list_fanout ();
	// Returns a list of probable reps and their weight
	std::vector<peer_information> list_probable_rep_weights ();
	// Get the next peer for attempting bootstrap
	rai::endpoint bootstrap_peer ();
	// Purge any peer where last_contact < time_point and return what was left
	std::vector<rai::peer_information> purge_list (std::chrono::steady_clock::time_point const &);
	void purge_syn_cookies (std::chrono::steady_clock::time_point const &);
	std::vector<rai::endpoint> rep_crawl ();
	bool rep_response (rai::endpoint const &, rai::account const &, rai::amount const &);
	void rep_request (rai::endpoint const &);
	// Should we reach out to this endpoint with a keepalive message
	bool reachout (rai::endpoint const &);
	// Returns boost::none if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	boost::optional<rai::uint256_union> assign_syn_cookie (rai::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate_syn_cookie (rai::endpoint const &, rai::account, rai::signature);
	size_t size ();
	size_t size_sqrt ();
	rai::uint128_t total_weight ();
	rai::uint128_t online_weight_minimum;
	bool empty ();
	std::mutex mutex;
	rai::endpoint self;
	boost::multi_index_container<
	peer_information,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<peer_information, rai::endpoint, &peer_information::endpoint>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_contact>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_attempt>, std::greater<std::chrono::steady_clock::time_point>>,
	boost::multi_index::random_access<>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_bootstrap_attempt>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_rep_request>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, rai::amount, &peer_information::rep_weight>, std::greater<rai::amount>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::tag<peer_by_ip_addr>, boost::multi_index::member<peer_information, boost::asio::ip::address, &peer_information::ip_address>>>>
	peers;
	boost::multi_index_container<
	peer_attempt,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<peer_attempt, rai::endpoint, &peer_attempt::endpoint>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_attempt, std::chrono::steady_clock::time_point, &peer_attempt::last_attempt>>>>
	attempts;
	std::mutex syn_cookie_mutex;
	std::unordered_map<rai::endpoint, syn_cookie_info> syn_cookies;
	std::unordered_map<boost::asio::ip::address, unsigned> syn_cookies_per_ip;
	// Called when a new peer is observed
	std::function<void(rai::endpoint const &)> peer_observer;
	std::function<void()> disconnect_observer;
	// Number of peers to crawl for being a rep every period
	static size_t constexpr peers_per_crawl = 8;
	// Maximum number of peers per IP
	static size_t constexpr max_peers_per_ip = 10;
};
}
