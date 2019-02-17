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
#include <nano/lib/numbers.hpp>
#include <nano/node/common.hpp>
#include <unordered_set>
#include <vector>

namespace nano
{
nano::endpoint map_endpoint_to_v6 (nano::endpoint const &);

/** Multi-index helper */
class peer_by_ip_addr
{
};

/** Multi-index helper */
class peer_attempt
{
public:
	nano::endpoint endpoint;
	std::chrono::steady_clock::time_point last_attempt;
};

/** Node handshake cookie */
class syn_cookie_info
{
public:
	nano::uint256_union cookie;
	std::chrono::steady_clock::time_point created_at;
};

/** Collects peer contact information */
class peer_information
{
public:
	peer_information (nano::endpoint const &, unsigned, boost::optional<nano::account> = boost::none);
	peer_information (nano::endpoint const &, std::chrono::steady_clock::time_point const &, std::chrono::steady_clock::time_point const &);
	nano::endpoint endpoint;
	boost::asio::ip::address ip_address;
	std::chrono::steady_clock::time_point last_contact;
	std::chrono::steady_clock::time_point last_attempt;
	std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
	std::chrono::steady_clock::time_point last_rep_request{ std::chrono::steady_clock::time_point () };
	std::chrono::steady_clock::time_point last_rep_response{ std::chrono::steady_clock::time_point () };
	nano::amount rep_weight{ 0 };
	nano::account probable_rep_account{ 0 };
	unsigned network_version{ nano::protocol_version };
	boost::optional<nano::account> node_id;
	bool operator< (nano::peer_information const &) const;
};

/** Manages a set of disovered peers */
class peer_container
{
public:
	peer_container (nano::endpoint const &);
	// We were contacted by endpoint, update peers
	// Returns true if a Node ID handshake should begin
	bool contacted (nano::endpoint const &, unsigned);
	// Unassigned, reserved, self
	bool not_a_peer (nano::endpoint const &, bool);
	// Returns true if peer was already known
	bool known_peer (nano::endpoint const &);
	// Notify of peer we received from
	bool insert (nano::endpoint const &, unsigned, bool = false, boost::optional<nano::account> = boost::none);
	std::unordered_set<nano::endpoint> random_set (size_t);
	void random_fill (std::array<nano::endpoint, 8> &);
	// Request a list of the top known representatives
	std::vector<peer_information> representatives (size_t);
	// List of all peers
	std::deque<nano::endpoint> list ();
	std::vector<peer_information> list_vector (size_t);
	// A list of random peers sized for the configured rebroadcast fanout
	std::deque<nano::endpoint> list_fanout ();
	// Returns a list of probable reps and their weight
	std::vector<peer_information> list_probable_rep_weights ();
	// Get the next peer for attempting bootstrap
	nano::endpoint bootstrap_peer ();
	// Purge any peer where last_contact < time_point and return what was left
	std::vector<nano::peer_information> purge_list (std::chrono::steady_clock::time_point const &);
	void purge_syn_cookies (std::chrono::steady_clock::time_point const &);
	std::vector<nano::endpoint> rep_crawl ();
	bool rep_response (nano::endpoint const &, nano::account const &, nano::amount const &);
	void rep_request (nano::endpoint const &);
	// Should we reach out to this endpoint with a keepalive message
	bool reachout (nano::endpoint const &);
	// Returns boost::none if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	boost::optional<nano::uint256_union> assign_syn_cookie (nano::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate_syn_cookie (nano::endpoint const &, nano::account, nano::signature);
	size_t size ();
	size_t size_sqrt ();
	nano::uint128_t total_weight ();
	nano::uint128_t online_weight_minimum;
	bool empty ();
	std::mutex mutex;
	nano::endpoint self;
	boost::multi_index_container<
	peer_information,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<peer_information, nano::endpoint, &peer_information::endpoint>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_contact>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_attempt>, std::greater<std::chrono::steady_clock::time_point>>,
	boost::multi_index::random_access<>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_bootstrap_attempt>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_rep_request>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_information, nano::amount, &peer_information::rep_weight>, std::greater<nano::amount>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::tag<peer_by_ip_addr>, boost::multi_index::member<peer_information, boost::asio::ip::address, &peer_information::ip_address>>>>
	peers;
	boost::multi_index_container<
	peer_attempt,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::member<peer_attempt, nano::endpoint, &peer_attempt::endpoint>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::member<peer_attempt, std::chrono::steady_clock::time_point, &peer_attempt::last_attempt>>>>
	attempts;
	std::mutex syn_cookie_mutex;
	std::unordered_map<nano::endpoint, syn_cookie_info> syn_cookies;
	std::unordered_map<boost::asio::ip::address, unsigned> syn_cookies_per_ip;
	// Called when a new peer is observed
	std::function<void(nano::endpoint const &)> peer_observer;
	std::function<void()> disconnect_observer;
	// Number of peers to crawl for being a rep every period
	static size_t constexpr peers_per_crawl = 8;
	// Maximum number of peers per IP
	static size_t constexpr max_peers_per_ip = 10;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (peer_container & peer_container, const std::string & name);
}
