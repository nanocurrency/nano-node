#include <nano/node/node.hpp>
#include <nano/node/vote_cache.hpp>

nano::vote_cache::entry::entry (const nano::block_hash & hash) :
	hash{ hash }
{
}

bool nano::vote_cache::entry::vote (const nano::account & representative, const uint64_t & timestamp, const nano::uint128_t & rep_weight)
{
	auto existing = std::find_if (voters.begin (), voters.end (), [&representative] (auto const & item) { return item.first == representative; });
	if (existing != voters.end ())
	{
		// Update timestamp but tally remains unchanged as we already counter this rep
		if (timestamp > existing->second)
		{
			existing->second = timestamp;
		}
		return false;
	}
	else
	{
		// Vote from an unseen representative, add to list and update tally
		if (voters.size () < max_voters)
		{
			voters.emplace_back (representative, timestamp);
			tally += rep_weight;
			return true;
		}
		else
		{
			return false;
		}
	}
}

void nano::vote_cache::entry::fill (std::shared_ptr<nano::election> const election) const
{
	for (const auto & [rep, timestamp] : voters)
	{
		election->vote (rep, timestamp, hash);
	}
}

nano::vote_cache::vote_cache (std::size_t max_size) :
	max_size{ max_size }
{
}

void nano::vote_cache::vote (const nano::block_hash & hash, const std::shared_ptr<nano::vote> vote)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	auto weight = rep_weight_query (vote->account);
	vote_impl (hash, vote->account, vote->timestamp (), weight);
}

void nano::vote_cache::vote_impl (const nano::block_hash & hash, const nano::account & representative, uint64_t const & timestamp, const nano::uint128_t & rep_weight)
{
	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		cache_by_hash.modify (existing, [&representative, &timestamp, &rep_weight] (entry & ent) {
			ent.vote (representative, timestamp, rep_weight);
		});
	}
	else
	{
		entry ent{ hash };
		ent.vote (representative, timestamp, rep_weight);
		cache.get<tag_hash> ().insert (std::move (ent));

		// When cache overflown remove the oldest entry
		if (cache.size () > max_size)
		{
			cache.get<tag_random_access> ().pop_front ();
		}
	}
}

bool nano::vote_cache::empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return cache.empty ();
}

std::size_t nano::vote_cache::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return cache.size ();
}

std::optional<nano::vote_cache::entry> nano::vote_cache::find (const nano::block_hash & hash) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		return *existing;
	}
	return {};
}

bool nano::vote_cache::erase (const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		cache_by_hash.erase (existing);
		return true;
	}
	return false;
}

nano::vote_cache::entry nano::vote_cache::peek () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	debug_assert (!cache.empty ());
	auto & cache_by_tally = cache.get<tag_tally> ();
	auto it = std::prev (cache_by_tally.end ());
	entry ent = *it;
	return ent;
}

std::optional<nano::vote_cache::entry> nano::vote_cache::pop (nano::uint128_t const & min_tally)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (cache.empty ())
	{
		return {};
	}
	auto & cache_by_tally = cache.get<tag_tally> ();
	auto it = std::prev (cache_by_tally.end ());
	entry ent = *it;
	if (ent.tally < min_tally)
	{
		return {};
	}
	cache_by_tally.erase (it);
	return ent;
}
