#include <nano/lib/container_info.hpp>
#include <nano/node/peer_blocklist.hpp>
#include <nano/node/transport/transport.hpp>

namespace
{
boost::asio::ip::address normalize (boost::asio::ip::address const & address)
{
	return nano::transport::mapped_from_v4_or_v6 (address);
}
}

bool nano::peer_blocklist::contains (nano::account const & node_id) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return node_ids.contains (node_id);
}

bool nano::peer_blocklist::contains (boost::asio::ip::address const & address) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return addresses.contains (normalize (address));
}

bool nano::peer_blocklist::add (nano::account const & node_id)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return node_ids.insert (node_id).second;
}

bool nano::peer_blocklist::add (boost::asio::ip::address const & address)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return addresses.insert (normalize (address)).second;
}

bool nano::peer_blocklist::remove (nano::account const & node_id)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return node_ids.erase (node_id) > 0;
}

bool nano::peer_blocklist::remove (boost::asio::ip::address const & address)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return addresses.erase (normalize (address)) > 0;
}

std::size_t nano::peer_blocklist::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return node_ids.size () + addresses.size ();
}

bool nano::peer_blocklist::empty () const
{
	return size () == 0;
}

nano::container_info nano::peer_blocklist::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("node_ids", node_ids);
	info.put ("addresses", addresses);
	return info;
}
