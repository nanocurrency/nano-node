#include <nano/lib/container_info.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/recently_confirmed_cache.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

nano::recently_confirmed_cache::recently_confirmed_cache (std::size_t max_size_a) :
	max_size{ max_size_a }
{
	release_assert (max_size > 0);
}

void nano::recently_confirmed_cache::put (nano::qualified_root const & root, nano::block_hash const & hash, nano::election_status const & status)
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	// A root or hash already present keeps its entry
	if (by_root.contains (root) || by_hash.contains (hash))
	{
		return;
	}

	std::size_t slot;
	if (slots.size () < max_size)
	{
		slot = slots.size ();
		slots.push_back ({ root, hash, status, true });
	}
	else
	{
		slot = next;
		release (slot);
		slots[slot] = { root, hash, status, true };
	}
	next = (slot + 1) % max_size;

	by_root.emplace (root, slot);
	by_hash.emplace (hash, slot);
	++live;
}

void nano::recently_confirmed_cache::release (std::size_t slot)
{
	auto & entry = slots[slot];
	if (entry.live)
	{
		by_root.erase (entry.root);
		by_hash.erase (entry.hash);
		entry.live = false;
		--live;
	}
}

void nano::recently_confirmed_cache::erase (nano::block_hash const & hash)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	if (auto existing = by_hash.find (hash); existing != by_hash.end ())
	{
		release (existing->second);
	}
}

void nano::recently_confirmed_cache::clear ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	slots.clear ();
	by_root.clear ();
	by_hash.clear ();
	next = 0;
	live = 0;
}

bool nano::recently_confirmed_cache::contains (nano::block_hash const & hash) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return by_hash.contains (hash);
}

bool nano::recently_confirmed_cache::contains (nano::qualified_root const & root) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return by_root.contains (root);
}

std::size_t nano::recently_confirmed_cache::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return live;
}

auto nano::recently_confirmed_cache::latency_percentiles () const -> latency_stats
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	if (live == 0)
	{
		return {};
	}
	std::vector<uint64_t> durations;
	durations.reserve (live);
	for (auto const & entry : slots)
	{
		if (entry.live)
		{
			durations.push_back (entry.status.election_duration.count ());
		}
	}
	lock.unlock ();
	std::sort (durations.begin (), durations.end ());
	auto percentile = [&durations] (double p) -> uint32_t {
		auto n = durations.size ();
		auto index = static_cast<size_t> (std::ceil (p / 100.0 * n)) - 1;
		release_assert (index < durations.size ());
		return static_cast<uint32_t> (durations[index]);
	};
	return {
		.p50 = percentile (50),
		.p90 = percentile (90),
		.p99 = percentile (99)
	};
}

nano::container_info nano::recently_confirmed_cache::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("entries", slots);
	return info;
}
