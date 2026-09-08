#include <nano/lib/container_info.hpp>
#include <nano/node/peer_blacklist.hpp>
#include <nano/node/transport/transport.hpp>

#include <optional>
#include <variant>

namespace
{
using entry_t = std::variant<nano::account, boost::asio::ip::address>;

std::optional<entry_t> parse (std::string const & entry)
{
	nano::account node_id;
	if (!node_id.decode_node_id (entry))
	{
		return node_id;
	}
	boost::system::error_code ec;
	auto const address = boost::asio::ip::make_address (entry, ec);
	if (!ec)
	{
		return address;
	}
	return std::nullopt;
}

// Peers connect over v4 mapped v6 addresses, entries are kept in the same form so both spellings of an address match
boost::asio::ip::address normalize (boost::asio::ip::address const & address)
{
	return nano::transport::mapped_from_v4_or_v6 (address);
}
}

bool nano::peer_blacklist::valid (std::string const & entry)
{
	return parse (entry).has_value ();
}

bool nano::peer_blacklist::add (std::string const & entry)
{
	auto const parsed = parse (entry);
	if (!parsed)
	{
		return false;
	}
	std::visit ([this] (auto const & value) { add (value); }, *parsed);
	return true;
}

void nano::peer_blacklist::add (nano::account const & node_id)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	node_ids.insert (node_id);
}

void nano::peer_blacklist::add (boost::asio::ip::address const & address)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	addresses.insert (normalize (address));
}

bool nano::peer_blacklist::blocked (nano::account const & node_id) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return node_ids.contains (node_id);
}

bool nano::peer_blacklist::blocked (boost::asio::ip::address const & address) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return addresses.contains (normalize (address));
}

void nano::peer_blacklist::remove (nano::account const & node_id)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	node_ids.erase (node_id);
}

void nano::peer_blacklist::remove (boost::asio::ip::address const & address)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	addresses.erase (normalize (address));
}

std::size_t nano::peer_blacklist::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return node_ids.size () + addresses.size ();
}

nano::container_info nano::peer_blacklist::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("node_ids", node_ids);
	info.put ("addresses", addresses);
	return info;
}
