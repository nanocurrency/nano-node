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
		election->vote (rep, timestamp, hash, election_vote_source::vote_cache);
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
	vote_impl_locked (hash, vote->account, vote->timestamp (), weight);
}

void nano::vote_cache::vote_impl_locked (const nano::block_hash & hash, const nano::account & representative, uint64_t const & timestamp, const nano::uint128_t & rep_weight)
{
	/**
	 * If there is no cache entry for the block hash, create a new entry for cache and queue.
	 * Otherwise update existing cache entry and, if queue still contains entry for the block hash, update queue entry
	 */
	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		bool success = cache_by_hash.modify (existing, [&representative, &timestamp, &rep_weight] (entry & ent) {
			ent.vote (representative, timestamp, rep_weight);
		});
		if (success) // Should never fail, but a check ensures the iterator `existing` is valid
		{
			auto & queue_by_hash = queue.get<tag_hash> ();
			if (auto queue_existing = queue_by_hash.find (hash); queue_existing != queue_by_hash.end ())
			{
				queue_by_hash.modify (queue_existing, [&existing] (queue_entry & ent) {
					ent.tally = existing->tally;
				});
			}
		}
	}
	else
	{
		entry cache_entry{ hash };
		cache_entry.vote (representative, timestamp, rep_weight);

		cache.get<tag_hash> ().insert (cache_entry);

		// If a stale entry for the same hash already exists in queue, replace it by a new entry with fresh tally
		auto & queue_by_hash = queue.get<tag_hash> ();
		if (auto queue_existing = queue_by_hash.find (hash); queue_existing != queue_by_hash.end ())
		{
			queue_by_hash.erase (queue_existing);
		}
		queue_by_hash.insert ({ hash, cache_entry.tally });

		trim_overflow_locked ();
	}
}

bool nano::vote_cache::cache_empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return cache.empty ();
}

bool nano::vote_cache::queue_empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return queue.empty ();
}

std::size_t nano::vote_cache::cache_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return cache.size ();
}

std::size_t nano::vote_cache::queue_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return queue.size ();
}

std::optional<nano::vote_cache::entry> nano::vote_cache::find (const nano::block_hash & hash) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return find_locked (hash);
}

bool nano::vote_cache::erase (const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	bool result = false;
	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		cache_by_hash.erase (existing);
		result = true;
	}
	auto & queue_by_hash = queue.get<tag_hash> ();
	if (auto existing = queue_by_hash.find (hash); existing != queue_by_hash.end ())
	{
		queue_by_hash.erase (existing);
	}
	return result;
}

std::optional<nano::vote_cache::entry> nano::vote_cache::pop (nano::uint128_t const & min_tally)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (!queue.empty ())
	{
		auto & queue_by_tally = queue.get<tag_tally> ();
		auto top = std::prev (queue_by_tally.end ()); // Iterator to element with the highest tally
		if (auto maybe_cache_entry = find_locked (top->hash); maybe_cache_entry)
		{
			// Here we check whether our best candidate passes the minimum vote tally threshold
			// If yes, erase it from the queue (but still keep the votes in cache)
			if (maybe_cache_entry->tally >= min_tally)
			{
				queue_by_tally.erase (top);
				return maybe_cache_entry.value ();
			}
		}
	}
	return {};
}

std::optional<nano::vote_cache::entry> nano::vote_cache::peek (nano::uint128_t const & min_tally) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (!queue.empty ())
	{
		auto & queue_by_tally = queue.get<tag_tally> ();
		auto top = std::prev (queue_by_tally.end ()); // Iterator to element with the highest tally
		if (auto maybe_cache_entry = find_locked (top->hash); maybe_cache_entry)
		{
			if (maybe_cache_entry->tally >= min_tally)
			{
				return maybe_cache_entry.value ();
			}
		}
	}
	return {};
}

void nano::vote_cache::trigger (const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto & queue_by_hash = queue.get<tag_hash> ();
	// Only reinsert to queue if it is not already in queue and there are votes in passive cache
	if (auto existing_queue = queue_by_hash.find (hash); existing_queue == queue_by_hash.end ())
	{
		if (auto maybe_cache_entry = find_locked (hash); maybe_cache_entry)
		{
			queue_by_hash.insert ({ hash, maybe_cache_entry->tally });

			trim_overflow_locked ();
		}
	}
}

std::optional<nano::vote_cache::entry> nano::vote_cache::find_locked (const nano::block_hash & hash) const
{
	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		return *existing;
	}
	return {};
}

void nano::vote_cache::trim_overflow_locked ()
{
	// When cache overflown remove the oldest entry
	if (cache.size () > max_size)
	{
		cache.get<tag_random_access> ().pop_front ();
	}
	if (queue.size () > max_size)
	{
		queue.get<tag_random_access> ().pop_front ();
	}
}