#include <nano/lib/tomlconfig.hpp>
#include <nano/node/node.hpp>
#include <nano/node/vote_cache.hpp>

/*
 * entry
 */

nano::vote_cache::entry::entry (const nano::block_hash & hash) :
	hash_m{ hash }
{
}

bool nano::vote_cache::entry::vote (const nano::account & representative, const uint64_t & timestamp, const nano::uint128_t & rep_weight, std::size_t max_voters)
{
	bool updated = vote_impl (representative, timestamp, rep_weight, max_voters);
	if (updated)
	{
		last_vote_m = std::chrono::steady_clock::now ();
	}
	return updated;
}

bool nano::vote_cache::entry::vote_impl (const nano::account & representative, const uint64_t & timestamp, const nano::uint128_t & rep_weight, std::size_t max_voters)
{
	auto existing = std::find_if (voters_m.begin (), voters_m.end (), [&representative] (auto const & item) { return item.representative == representative; });
	if (existing != voters_m.end ())
	{
		// We already have a vote from this rep
		// Update timestamp if newer but tally remains unchanged as we already counted this rep weight
		// It is not essential to keep tally up to date if rep voting weight changes, elections do tally calculations independently, so in the worst case scenario only our queue ordering will be a bit off
		if (timestamp > existing->timestamp)
		{
			existing->timestamp = timestamp;
			if (nano::vote::is_final_timestamp (timestamp))
			{
				final_tally_m += rep_weight;
			}
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		// Vote from an unseen representative, add to list and update tally
		if (voters_m.size () < max_voters)
		{
			voters_m.push_back ({ representative, timestamp });
			tally_m += rep_weight;
			if (nano::vote::is_final_timestamp (timestamp))
			{
				final_tally_m += rep_weight;
			}
			return true;
		}
		else
		{
			return false;
		}
	}
}

std::size_t nano::vote_cache::entry::fill (std::shared_ptr<nano::election> const & election) const
{
	std::size_t inserted = 0;
	for (const auto & entry : voters_m)
	{
		auto [is_replay, processed] = election->vote (entry.representative, entry.timestamp, hash_m, nano::election::vote_source::cache);
		if (processed)
		{
			inserted++;
		}
	}
	return inserted;
}

std::size_t nano::vote_cache::entry::size () const
{
	return voters_m.size ();
}

nano::block_hash nano::vote_cache::entry::hash () const
{
	return hash_m;
}

nano::uint128_t nano::vote_cache::entry::tally () const
{
	return tally_m;
}

nano::uint128_t nano::vote_cache::entry::final_tally () const
{
	return final_tally_m;
}

std::vector<nano::vote_cache::entry::voter_entry> nano::vote_cache::entry::voters () const
{
	return voters_m;
}

std::chrono::steady_clock::time_point nano::vote_cache::entry::last_vote () const
{
	return last_vote_m;
}

/*
 * vote_cache
 */

nano::vote_cache::vote_cache (vote_cache_config const & config_a, nano::stats & stats_a) :
	config{ config_a },
	stats{ stats_a },
	cleanup_interval{ config_a.age_cutoff / 2 }
{
}

void nano::vote_cache::vote (const nano::block_hash & hash, const std::shared_ptr<nano::vote> vote)
{
	// Assert that supplied hash corresponds to a one of the hashes stored in vote
	debug_assert (std::find (vote->hashes.begin (), vote->hashes.end (), hash) != vote->hashes.end ());

	auto const representative = vote->account;
	auto const timestamp = vote->timestamp ();
	auto const rep_weight = rep_weight_query (representative);

	nano::unique_lock<nano::mutex> lock{ mutex };

	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		stats.inc (nano::stat::type::vote_cache, nano::stat::detail::update);

		cache_by_hash.modify (existing, [this, &representative, &timestamp, &rep_weight] (entry & ent) {
			ent.vote (representative, timestamp, rep_weight, config.max_voters);
		});
	}
	else
	{
		stats.inc (nano::stat::type::vote_cache, nano::stat::detail::insert);

		entry cache_entry{ hash };
		cache_entry.vote (representative, timestamp, rep_weight, config.max_voters);

		cache.get<tag_hash> ().insert (cache_entry);

		// When cache overflown remove the oldest entry
		if (cache.size () > config.max_size)
		{
			cache.get<tag_sequenced> ().pop_front ();
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

	bool result = false;
	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		cache_by_hash.erase (existing);
		result = true;
	}
	return result;
}

void nano::vote_cache::clear ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	cache.clear ();
}

std::vector<nano::vote_cache::top_entry> nano::vote_cache::top (const nano::uint128_t & min_tally)
{
	stats.inc (nano::stat::type::vote_cache, nano::stat::detail::top);

	std::vector<top_entry> results;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };

		if (cleanup_interval.elapsed ())
		{
			cleanup ();
		}

		for (auto & entry : cache.get<tag_tally> ())
		{
			if (entry.tally () < min_tally)
			{
				break;
			}
			results.push_back ({ entry.hash (), entry.tally (), entry.final_tally () });
		}
	}

	// Sort by final tally then by normal tally, descending
	std::sort (results.begin (), results.end (), [] (auto const & a, auto const & b) {
		if (a.final_tally == b.final_tally)
		{
			return a.tally > b.tally;
		}
		else
		{
			return a.final_tally > b.final_tally;
		}
	});

	return results;
}

void nano::vote_cache::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	stats.inc (nano::stat::type::vote_cache, nano::stat::detail::cleanup);

	auto const cutoff = std::chrono::steady_clock::now () - config.age_cutoff;

	erase_if (cache, [cutoff] (auto const & entry) {
		return entry.last_vote () < cutoff;
	});
}

std::unique_ptr<nano::container_info_component> nano::vote_cache::collect_container_info (const std::string & name) const
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cache", size (), sizeof (ordered_cache::value_type) }));
	return composite;
}

/*
 * vote_cache_config
 */

nano::error nano::vote_cache_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("max_size", max_size, "Maximum number of blocks to cache votes for. \ntype:uint64");
	toml.put ("max_voters", max_voters, "Maximum number of voters to cache per block. \ntype:uint64");
	toml.put ("age_cutoff", age_cutoff.count (), "Maximum age of votes to keep in cache. \ntype:seconds");

	return toml.get_error ();
}

nano::error nano::vote_cache_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("max_size", max_size);
	toml.get ("max_voters", max_voters);

	auto age_cutoff_l = age_cutoff.count ();
	toml.get ("age_cutoff", age_cutoff_l);
	age_cutoff = std::chrono::seconds{ age_cutoff_l };

	return toml.get_error ();
}