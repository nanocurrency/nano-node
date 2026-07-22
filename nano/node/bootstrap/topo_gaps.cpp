#include <nano/lib/container_info.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/bootstrap/topo_gaps.hpp>

namespace nano::bootstrap
{
topo_gaps::topo_gaps (topo_scan_config const & config_a, nano::stats & stats_a) :
	config{ config_a },
	stats{ stats_a }
{
}

void topo_gaps::track (nano::block_hash const & hash, nano::account const & account, std::chrono::steady_clock::time_point now)
{
	auto & by_hash = gaps.get<tag_hash> ();
	auto it = by_hash.find (hash);
	if (it == by_hash.end ())
	{
		by_hash.insert (entry{ .hash = hash, .account = account, .timestamp = now });
		stats.inc (nano::stat::type::bootstrap_topo_gaps, nano::stat::detail::tracked);
	}
	else
	{
		// Re-gapped while still in flight: keep it alive so eviction doesn't fire mid-resolution
		by_hash.modify (it, [now] (entry & e) { e.timestamp = now; });
	}
}

void topo_gaps::resolve (nano::block_hash const & hash)
{
	auto & by_hash = gaps.get<tag_hash> ();
	if (auto it = by_hash.find (hash); it != by_hash.end ())
	{
		by_hash.erase (it);
		stats.inc (nano::stat::type::bootstrap_topo_gaps, nano::stat::detail::resolved);
	}
}

void topo_gaps::rollback (nano::account const & account)
{
	auto & by_account = gaps.get<tag_account> ();
	if (auto const erased = by_account.erase (account); erased > 0)
	{
		stats.add (nano::stat::type::bootstrap_topo_gaps, nano::stat::detail::rolled_back, erased);
	}
}

void topo_gaps::evict (std::chrono::steady_clock::time_point now)
{
	auto const cutoff = now - config.gap_ttl;
	auto & by_timestamp = gaps.get<tag_timestamp> ();
	// Entries are ordered by timestamp, so the expired ones form a prefix
	for (auto it = by_timestamp.begin (); it != by_timestamp.end () && it->timestamp < cutoff;)
	{
		it = by_timestamp.erase (it);
		stats.inc (nano::stat::type::bootstrap_topo_gaps, nano::stat::detail::evicted);
	}
}

std::size_t topo_gaps::count () const
{
	return gaps.size ();
}

void topo_gaps::reset ()
{
	gaps.clear ();
}

nano::container_info topo_gaps::container_info () const
{
	nano::container_info info;
	info.put ("total", gaps.size ());
	return info;
}
}
