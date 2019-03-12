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
	peer_information (std::shared_ptr<nano::transport::channel_udp>);
	peer_information (std::shared_ptr<nano::transport::channel_udp>, std::chrono::steady_clock::time_point const &);
	std::shared_ptr<nano::transport::channel_udp> sink;
	boost::asio::ip::address ip_address () const;
	nano::endpoint endpoint () const;
	std::reference_wrapper<nano::transport::channel const> sink_ref () const;
	std::chrono::steady_clock::time_point last_contact;
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
	// Called when a new peer is observed
	std::function<void(std::shared_ptr<nano::transport::channel>)> peer_observer;
	// Number of peers to crawl for being a rep every period
	static size_t constexpr peers_per_crawl = 8;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (peer_container & peer_container, const std::string & name);
}
