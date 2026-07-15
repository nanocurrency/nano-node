#include <nano/lib/container_info.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/peer_pool.hpp>
#include <nano/node/transport/channel.hpp>

#include <algorithm>
#include <iterator>
#include <utility>

namespace nano::bootstrap
{
peer_pool::peer_pool (nano::bootstrap_config const & config_a) :
	config{ config_a }
{
}

auto peer_pool::acquire (nano::node_capabilities_flags required, std::span<nano::account const> exclude, nano::transport::traffic_type traffic) -> peer_pool::acquire_result
{
	bool capable_present = false; // Some connected peer satisfies the capability requirement
	bool candidate_present = false; // Some capable peer is not excluded, though it may be at capacity

	auto excluded = [&exclude] (entry const & entry) {
		return std::find (exclude.begin (), exclude.end (), entry.node_id) != exclude.end ();
	};

	auto const & by_outstanding = entries.get<tag_outstanding> ();
	auto const available_end = config.channel_limit == 0 ? by_outstanding.end () : by_outstanding.lower_bound (static_cast<uint64_t> (config.channel_limit));

	for (auto it = by_outstanding.begin (); it != available_end; ++it)
	{
		auto const & entry = *it;
		if (!entry.capable (required))
		{
			continue;
		}
		capable_present = true;
		if (excluded (entry))
		{
			continue; // Already used by the requesting round
		}
		candidate_present = true;
		if (entry.channel->max (traffic))
		{
			continue; // Send queue full, may free up later
		}

		auto channel = entry.channel;
		auto node_id = entry.node_id;
		auto & by_channel = entries.get<tag_channel> ();
		auto existing = by_channel.find (channel);
		debug_assert (existing != by_channel.end ());
		[[maybe_unused]] auto success = by_channel.modify (existing, [] (auto & entry) {
			++entry.outstanding;
		});
		debug_assert (success);
		return { channel, peer_acquire_status::acquired, node_id };
	}

	for (auto it = available_end; it != by_outstanding.end (); ++it)
	{
		auto const & entry = *it;
		if (!entry.capable (required))
		{
			continue;
		}
		capable_present = true;
		if (excluded (entry))
		{
			continue;
		}
		candidate_present = true;
		break;
	}

	if (candidate_present)
	{
		return { nullptr, peer_acquire_status::busy };
	}
	if (capable_present)
	{
		return { nullptr, peer_acquire_status::exhausted };
	}
	return { nullptr, peer_acquire_status::no_peers };
}

void peer_pool::release (std::shared_ptr<nano::transport::channel> const & channel)
{
	auto & by_channel = entries.get<tag_channel> ();
	if (auto it = by_channel.find (channel); it != by_channel.end ())
	{
		[[maybe_unused]] auto success = by_channel.modify (it, [] (auto & entry) {
			entry.decay ();
		});
		debug_assert (success);
	}
}

bool peer_pool::has_candidate (nano::node_capabilities_flags required, std::span<nano::account const> exclude) const
{
	return std::any_of (entries.begin (), entries.end (), [&] (auto const & item) {
		auto const & entry = item;
		return entry.capable (required) && std::find (exclude.begin (), exclude.end (), entry.node_id) == exclude.end ();
	});
}

peer_probe_status peer_pool::probe (nano::node_capabilities_flags required, std::span<nano::account const> exclude) const
{
	auto excluded = [&exclude] (entry const & entry) {
		return std::find (exclude.begin (), exclude.end (), entry.node_id) != exclude.end ();
	};

	auto const & by_outstanding = entries.get<tag_outstanding> ();
	auto const available_end = config.channel_limit == 0 ? by_outstanding.end () : by_outstanding.lower_bound (static_cast<uint64_t> (config.channel_limit));

	for (auto it = by_outstanding.begin (); it != available_end; ++it)
	{
		auto const & entry = *it;
		if (entry.capable (required) && !excluded (entry))
		{
			return peer_probe_status::available;
		}
	}

	for (auto it = available_end; it != by_outstanding.end (); ++it)
	{
		auto const & entry = *it;
		if (entry.capable (required) && !excluded (entry))
		{
			return peer_probe_status::busy;
		}
	}

	return peer_probe_status::none;
}

void peer_pool::update (std::deque<std::shared_ptr<nano::transport::channel>> const & channels)
{
	auto & by_channel = entries.get<tag_channel> ();

	// Track new channels and refresh the cached identity of known ones
	for (auto const & channel : channels)
	{
		if (auto it = by_channel.find (channel); it != by_channel.end ())
		{
			[[maybe_unused]] auto success = by_channel.modify (it, [&channel] (auto & entry) {
				entry.channel = channel;
				entry.node_id = channel->get_node_id ();
				entry.capabilities = channel->get_flags ();
			});
			debug_assert (success);
		}
		else
		{
			by_channel.emplace (channel, channel->get_node_id (), channel->get_flags ());
		}
	}

	// Drop closed channels
	for (auto it = by_channel.begin (); it != by_channel.end ();)
	{
		if (!it->channel->alive ())
		{
			it = by_channel.erase (it);
		}
		else
		{
			++it;
		}
	}
}

void peer_pool::decay ()
{
	auto & by_channel = entries.get<tag_channel> ();
	for (auto it = by_channel.begin (), n = by_channel.end (); it != n; ++it)
	{
		[[maybe_unused]] auto success = by_channel.modify (it, [] (auto & entry) {
			entry.decay ();
		});
		debug_assert (success);
	}
}

void peer_pool::reset ()
{
	entries.clear ();
}

std::size_t peer_pool::size () const
{
	return entries.size ();
}

std::size_t peer_pool::available () const
{
	if (config.channel_limit == 0)
	{
		return entries.size ();
	}

	auto const & by_outstanding = entries.get<tag_outstanding> ();
	return static_cast<std::size_t> (std::distance (by_outstanding.begin (), by_outstanding.lower_bound (static_cast<uint64_t> (config.channel_limit))));
}

nano::container_info peer_pool::container_info () const
{
	nano::container_info info;
	info.put ("tracked", size ());
	info.put ("available", available ());
	return info;
}

/*
 *
 */

nano::stat::detail to_stat_detail (peer_acquire_status status)
{
	return nano::enum_convert<nano::stat::detail> (status);
}

nano::stat::detail to_stat_detail (peer_probe_status status)
{
	return nano::enum_convert<nano::stat::detail> (status);
}

/*
 * peer_pool::entry
 */

peer_pool::entry::entry (std::shared_ptr<nano::transport::channel> channel_a, nano::account node_id_a, nano::node_capabilities_flags capabilities_a) :
	channel{ std::move (channel_a) },
	node_id{ node_id_a },
	capabilities{ capabilities_a }
{
}
}
