#pragma once

#include <boost/asio/ip/address.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
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
#include <nano/node/transport/transport.hpp>
#include <unordered_set>
#include <vector>

namespace nano
{
class node;
namespace transport
{
	class channel_udp;
}

nano::endpoint map_endpoint_to_v6 (nano::endpoint const &);

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
	peer_information (std::shared_ptr<nano::transport::channel_udp>, boost::optional<nano::account> = boost::none);
	peer_information (std::shared_ptr<nano::transport::channel_udp>, std::chrono::steady_clock::time_point const &);
	std::shared_ptr<nano::transport::channel_udp> sink;
	boost::asio::ip::address ip_address () const;
	nano::endpoint endpoint () const;
	std::reference_wrapper<nano::transport::channel const> sink_ref () const;
	std::chrono::steady_clock::time_point last_contact;
	boost::optional<nano::account> node_id;
	bool operator< (nano::peer_information const &) const;
};

/** Manages a set of disovered peers */
class peer_container
{
public:
	class rep_weight_tag
	{
	};
	class last_rep_request_tag
	{
	};
	class last_contact_tag
	{
	};
	class sink_ref_tag
	{
	};
	class random_access_tag
	{
	};
	peer_container (nano::node &);
	// We were contacted by endpoint, update peers
	void contacted (nano::endpoint const &);
	// Notify of peer we received from
	bool insert (nano::endpoint const &, unsigned, bool = false, boost::optional<nano::account> = boost::none);
	std::vector<peer_information> list_vector (size_t);
	// A list of random peers sized for the configured rebroadcast fanout
	std::deque<std::shared_ptr<nano::transport::channel_udp>> list_fanout ();
	// Returns a list of probable reps and their weight
	std::vector<peer_information> list_probable_rep_weights ();
	// Purge any peer where last_contact < time_point and return what was left
	void purge_list (std::chrono::steady_clock::time_point const &);
	void purge_syn_cookies (std::chrono::steady_clock::time_point const &);
	// Should we reach out to this endpoint with a keepalive message
	bool reachout (nano::endpoint const &, bool = false);
	// Returns boost::none if the IP is rate capped on syn cookie requests,
	// or if the endpoint already has a syn cookie query
	boost::optional<nano::uint256_union> assign_syn_cookie (nano::endpoint const &);
	// Returns false if valid, true if invalid (true on error convention)
	// Also removes the syn cookie from the store if valid
	bool validate_syn_cookie (nano::endpoint const &, nano::account, nano::signature);
	size_t size ();
	size_t size_sqrt ();
	bool empty ();
	void ongoing_keepalive ();
	std::mutex mutex;
	nano::node & node;
	boost::multi_index_container<
	peer_information,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::tag<sink_ref_tag>, boost::multi_index::const_mem_fun<peer_information, std::reference_wrapper<nano::transport::channel const>, &peer_information::sink_ref>>,
	boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_contact_tag>, boost::multi_index::member<peer_information, std::chrono::steady_clock::time_point, &peer_information::last_contact>>,
	boost::multi_index::random_access<boost::multi_index::tag<random_access_tag>>>>
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
	std::function<void(std::shared_ptr<nano::transport::channel>)> peer_observer;
	std::function<void()> disconnect_observer;
	// Number of peers to crawl for being a rep every period
	static size_t constexpr peers_per_crawl = 8;
	static std::chrono::seconds constexpr period = nano::is_test_network ? std::chrono::seconds (1) : std::chrono::seconds (60);
	static std::chrono::seconds constexpr cutoff = period * 5;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (peer_container & peer_container, const std::string & name);
}
