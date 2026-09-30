#include <nano/lib/assert.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/container_info.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/peer_blocklist.hpp>
#include <nano/node/transport/transport.hpp>

#include <functional>

namespace
{
boost::asio::ip::address normalize (boost::asio::ip::address const & address)
{
	return nano::transport::mapped_from_v4_or_v6 (address);
}

// Passes every entry of the string array at the present key to the callback until one is reported as an error
// Anything else at the key is an error too, so a mistyped list is never taken as empty
void read_entries (nano::tomlconfig & toml, std::string const & key, std::function<void (std::string const &)> const & callback)
{
	auto const value = toml.get_tree ()->get (key);
	debug_assert (value != nullptr);
	auto const array = value->as_array ();
	if (!array)
	{
		toml.get_error ().set (key + " must be an array of strings");
		return;
	}
	for (auto const & item : array->get ())
	{
		auto const text = item->as<std::string> ();
		if (!text)
		{
			toml.get_error ().set (key + " must be an array of strings");
			return;
		}
		callback (text->get ());
		if (toml.get_error ())
		{
			return;
		}
	}
}
}

/*
 * peer_blocklist_config
 */

nano::error nano::peer_blocklist_config::deserialize_toml (nano::tomlconfig & toml)
{
	auto blocklist_l = toml.get_optional_child ("blocklist");
	if (toml.get_error () || !blocklist_l)
	{
		return toml.get_error ();
	}

	if (blocklist_l->has_key ("node_ids"))
	{
		node_ids.clear ();
		read_entries (*blocklist_l, "node_ids", [this, &toml] (std::string const & entry) {
			nano::account node_id;
			// Only the node id spelling is accepted, an account address here is almost certainly a mistake
			if (!entry.starts_with ("node_") || node_id.decode_node_id (entry))
			{
				toml.get_error ().set ("Invalid node id: " + entry);
				return;
			}
			node_ids.push_back (node_id);
		});
	}

	if (!toml.get_error () && blocklist_l->has_key ("ip_addresses"))
	{
		ip_addresses.clear ();
		read_entries (*blocklist_l, "ip_addresses", [this, &toml] (std::string const & entry) {
			boost::system::error_code ec;
			auto const address = boost::asio::ip::make_address (entry, ec);
			if (ec)
			{
				toml.get_error ().set ("Invalid IP address: " + entry);
				return;
			}
			ip_addresses.push_back (address);
		});
	}

	return toml.get_error ();
}

nano::error nano::peer_blocklist_config::serialize_toml (nano::tomlconfig & toml) const
{
	nano::tomlconfig blocklist_l;

	auto node_ids_l = blocklist_l.create_array ("node_ids", "Node ids this node refuses to connect with, in either direction, e.g. [\"node_1abc...\", \"node_3xyz...\"]");
	for (auto const & node_id : node_ids)
	{
		node_ids_l->push_back (node_id.to_node_id ());
	}

	auto ip_addresses_l = blocklist_l.create_array ("ip_addresses", "IP addresses this node refuses to connect with, in either direction, e.g. [\"192.0.2.1\", \"2001:db8::1\"]");
	for (auto const & address : ip_addresses)
	{
		ip_addresses_l->push_back (address.to_string ());
	}

	toml.put_child ("blocklist", blocklist_l);
	return toml.get_error ();
}

nano::error nano::read_peer_blocklist_config (nano::peer_blocklist_config & config, std::filesystem::path const & data_path)
{
	return nano::read_config_file (config, nano::peer_blocklist_filename, data_path);
}

/*
 * peer_blocklist
 */

nano::peer_blocklist::peer_blocklist (peer_blocklist_config const & config)
{
	for (auto const & node_id : config.node_ids)
	{
		add (node_id);
	}
	for (auto const & address : config.ip_addresses)
	{
		add (address);
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
	return ip_addresses.contains (normalize (address));
}

bool nano::peer_blocklist::add (nano::account const & node_id)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return node_ids.insert (node_id).second;
}

bool nano::peer_blocklist::add (boost::asio::ip::address const & address)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ip_addresses.insert (normalize (address)).second;
}

bool nano::peer_blocklist::remove (nano::account const & node_id)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return node_ids.erase (node_id) > 0;
}

bool nano::peer_blocklist::remove (boost::asio::ip::address const & address)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ip_addresses.erase (normalize (address)) > 0;
}

std::size_t nano::peer_blocklist::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return node_ids.size () + ip_addresses.size ();
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
	info.put ("ip_addresses", ip_addresses);
	return info;
}
