#include <nano/lib/utility.hpp>
#include <nano/node/recently_confirmed_cache.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

nano::recently_confirmed_cache::recently_confirmed_cache (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

void nano::recently_confirmed_cache::put (const nano::qualified_root & root, const nano::block_hash & hash, nano::election_status const & status)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	entries.push_back (entry{ root, hash, status });
	if (entries.size () > max_size)
	{
		entries.pop_front ();
	}
}

void nano::recently_confirmed_cache::erase (const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	entries.get<tag_hash> ().erase (hash);
}

void nano::recently_confirmed_cache::clear ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	entries.clear ();
}

bool nano::recently_confirmed_cache::contains (const nano::block_hash & hash) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return entries.get<tag_hash> ().contains (hash);
}

bool nano::recently_confirmed_cache::contains (const nano::qualified_root & root) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return entries.get<tag_root> ().contains (root);
}

std::size_t nano::recently_confirmed_cache::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return entries.size ();
}

auto nano::recently_confirmed_cache::latency_percentiles () const -> latency_stats
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	if (entries.empty ())
	{
		return {};
	}

	std::vector<uint64_t> durations;
	durations.reserve (entries.size ());
	for (auto const & entry : entries)
	{
		durations.push_back (entry.status.election_duration.count ());
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
	info.put ("entries", entries);
	return info;
}
