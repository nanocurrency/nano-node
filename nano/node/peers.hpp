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
	std::shared_ptr<nano::transport::channel_udp> sink;
	boost::asio::ip::address ip_address () const;
	nano::endpoint endpoint () const;
	std::reference_wrapper<nano::transport::channel const> sink_ref () const;
	bool operator< (nano::peer_information const &) const;
};

/** Manages a set of disovered peers */
class peer_container
{
public:
	class sink_ref_tag
	{
	};
	class random_access_tag
	{
	};
	peer_container (nano::node &);
	// Notify of peer we received from
	bool insert (nano::endpoint const &, unsigned, bool = false);
	// Purge any peer where last_contact < time_point and return what was left
	void erase (nano::transport::channel const &);
	std::mutex mutex;
	nano::node & node;
	boost::multi_index_container<
	peer_information,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<boost::multi_index::tag<sink_ref_tag>, boost::multi_index::const_mem_fun<peer_information, std::reference_wrapper<nano::transport::channel const>, &peer_information::sink_ref>>,
	boost::multi_index::random_access<boost::multi_index::tag<random_access_tag>>>>
	peers;
	// Called when a new peer is observed
	std::function<void(std::shared_ptr<nano::transport::channel>)> peer_observer;
	// Number of peers to crawl for being a rep every period
	static size_t constexpr peers_per_crawl = 8;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (peer_container & peer_container, const std::string & name);
}
