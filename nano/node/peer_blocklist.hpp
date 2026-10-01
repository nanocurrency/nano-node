#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/endpoint_templ.hpp>
#include <nano/node/fwd.hpp>

#include <boost/asio/ip/address.hpp>

#include <deque>
#include <filesystem>
#include <unordered_set>

namespace nano
{
/**
 * Contents of the peer blocklist file, which lives in the data directory next to the other config files but is read separately from them.
 * The file holds a single [blocklist] table with the node_ids and ip_addresses lists; any other key is an error.
 */
class peer_blocklist_config final
{
public:
	nano::error deserialize_toml (nano::tomlconfig &);
	nano::error serialize_toml (nano::tomlconfig &) const;

public:
	std::deque<nano::account> node_ids;
	std::deque<boost::asio::ip::address> ip_addresses;
};

/** Reads the peer blocklist file from \p data_path into \p config; a missing file leaves the config unchanged and is not created */
nano::error read_peer_blocklist_config (nano::peer_blocklist_config & config, std::filesystem::path const & data_path);

/**
 * Peers this node refuses to connect with, given by node id or by IP address.
 * Addresses are refused before a connection is made and node ids once the handshake proves them, so a blocklisted peer never gets a channel in either direction.
 * IPv4 addresses are kept in the v4 mapped IPv6 form peers are seen with, so both spellings of an address match.
 */
class peer_blocklist final
{
public:
	peer_blocklist () = default;
	explicit peer_blocklist (peer_blocklist_config const &);

	bool contains (nano::account const & node_id) const;
	bool contains (boost::asio::ip::address const &) const;

	// @return false if the entry was already present
	bool add (nano::account const & node_id);
	bool add (boost::asio::ip::address const &);

	// @return false if the entry was not present
	bool remove (nano::account const & node_id);
	bool remove (boost::asio::ip::address const &);

	// Number of entries of both kinds
	std::size_t size () const;
	bool empty () const;

	nano::container_info container_info () const;

private:
	std::unordered_set<nano::account> node_ids;
	std::unordered_set<boost::asio::ip::address> ip_addresses;
	mutable nano::mutex mutex;
};
}
