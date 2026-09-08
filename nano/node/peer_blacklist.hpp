#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/endpoint_templ.hpp>
#include <nano/node/fwd.hpp>

#include <boost/asio/ip/address.hpp>

#include <string>
#include <unordered_set>

namespace nano
{
/**
 * Peers this node refuses to talk to, identified by node id or by IP address.
 * Addresses are refused before a connection is made, node ids once the handshake reveals them, so a blacklisted peer never gets a channel in either direction.
 */
class peer_blacklist final
{
public:
	// Whether the entry parses as a node id or an IP address
	static bool valid (std::string const & entry);

	// Add an entry given as a node id or an IP address
	// @return false if the entry is neither
	bool add (std::string const & entry);
	void add (nano::account const & node_id);
	void add (boost::asio::ip::address const &);

	bool blocked (nano::account const & node_id) const;
	bool blocked (boost::asio::ip::address const &) const;

	void remove (nano::account const & node_id);
	void remove (boost::asio::ip::address const &);

	std::size_t size () const;
	nano::container_info container_info () const;

private:
	std::unordered_set<nano::account> node_ids;
	std::unordered_set<boost::asio::ip::address> addresses;
	mutable nano::mutex mutex;
};
}
