#include <nano/lib/tomlconfig.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_router.hpp>

#include <ranges>

/*
 * entvote_cache_entryry
 */

nano::vote_cache_entry::vote_cache_entry (const nano::block_hash & hash) :
	hash_m{ hash }
{
}

bool nano::vote_cache_entry::vote (std::shared_ptr<nano::vote> const & vote, const nano::uint128_t & rep_weight, std::size_t max_voters)
{
	bool updated = vote_impl (vote, rep_weight, max_voters);
	if (updated)
	{
		last_vote_m = std::chrono::steady_clock::now ();
	}
	return updated;
}

bool nano::vote_cache_entry::vote_impl (std::shared_ptr<nano::vote> const & vote, const nano::uint128_t & rep_weight, std::size_t max_voters)
{
	auto const representative = vote->account;

	if (auto existing = voters.find (representative); existing != voters.end ())
	{
		// We already have a vote from this rep
		// Update timestamp if newer but tally remains unchanged as we already counted this rep weight
		// It is not essential to keep tally up to date if rep voting weight changes, elections do tally calculations independently, so in the worst case scenario only our queue ordering will be a bit off
		if (vote->timestamp () > existing->vote->timestamp ())
		{
			voters.modify (existing, [&vote, &rep_weight] (auto & existing) {
				existing.vote = vote;
				existing.weight = rep_weight;
			});
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		auto should_add = [&, this] () {
			if (voters.size () < max_voters)
			{
				return true;
			}
			else
			{
				release_assert (!voters.empty ());
				auto const & min_weight = voters.get<tag_weight> ().begin ()->weight;
				return rep_weight > min_weight;
			}
		};

		// Vote from a new representative, add it to the list and update tally
		if (should_add ())
		{
			voters.insert ({ representative, rep_weight, vote });

			// If we have reached the maximum number of voters, remove the lowest weight voter
			if (voters.size () >= max_voters)
			{
				release_assert (!voters.empty ());
				voters.get<tag_weight> ().erase (voters.get<tag_weight> ().begin ());
			}

			return true;
		}
		else
		{
			return false;
		}
	}
}

std::size_t nano::vote_cache_entry::size () const
{
	return voters.size ();
}

nano::block_hash nano::vote_cache_entry::hash () const
{
	return hash_m;
}

nano::uint128_t nano::vote_cache_entry::tally () const
{
	return std::accumulate (voters.begin (), voters.end (), nano::uint128_t{ 0 }, [] (auto sum, auto const & item) {
		return sum + item.weight;
	});
}

nano::uint128_t nano::vote_cache_entry::final_tally () const
{
	return std::accumulate (voters.begin (), voters.end (), nano::uint128_t{ 0 }, [] (auto sum, auto const & item) {
		return sum + (item.vote->is_final () ? item.weight : 0);
	});
}

std::vector<std::shared_ptr<nano::vote>> nano::vote_cache_entry::votes () const
{
	auto r = voters | std::views::transform ([] (auto const & item) { return item.vote; });
	return { r.begin (), r.end () };
}

std::chrono::steady_clock::time_point nano::vote_cache_entry::last_vote () const
{
	return last_vote_m;
}

/*
 * vote_cache
 */

nano::vote_cache::vote_cache (vote_cache_config const & config_a, nano::stats & stats_a) :
	config{ config_a },
	stats{ stats_a }
{
}

void nano::vote_cache::observe (const std::shared_ptr<nano::vote> & vote, nano::vote_source source, std::unordered_map<nano::block_hash, nano::vote_code> results)
{
	if (source != nano::vote_source::cache)
	{
		insert (vote, [&results] (nano::block_hash const & hash) {
			// This filters which hashes should be included in the vote cache
			if (auto it = results.find (hash); it != results.end ())
			{
				auto result = it->second;
				// Cache votes with a corresponding active election (indicated by `vote_code::vote`) in case that election gets dropped
				return result == nano::vote_code::vote || result == nano::vote_code::indeterminate;
			}
			debug_assert (false);
			return false;
		});
	}
}

void nano::vote_cache::insert (std::shared_ptr<nano::vote> const & vote, std::function<bool (nano::block_hash const &)> filter)
{
	auto const representative = vote->account;
	auto const timestamp = vote->timestamp ();
	auto const rep_weight = rep_weight_query (representative);

	nano::lock_guard<nano::mutex> lock{ mutex };

	for (auto const & hash : vote->hashes)
	{
		// Using filter callback here to avoid unnecessary relocking when processing large votes
		if (!filter (hash))
		{
			continue;
		}

		if (auto existing = cache.find (hash); existing != cache.end ())
		{
			stats.inc (nano::stat::type::vote_cache, nano::stat::detail::update);

			cache.modify (existing, [this, &vote, &rep_weight] (entry & ent) {
				ent.vote (vote, rep_weight, config.max_voters);
			});
		}
		else
		{
			stats.inc (nano::stat::type::vote_cache, nano::stat::detail::insert);

			entry cache_entry{ hash };
			cache_entry.vote (vote, rep_weight, config.max_voters);
			cache.insert (cache_entry);

			// Remove the oldest entry if we have reached the capacity limit
			if (cache.size () > config.max_size)
			{
				cache.get<tag_sequenced> ().pop_front ();
			}
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

std::vector<std::shared_ptr<nano::vote>> nano::vote_cache::find (const nano::block_hash & hash) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		return existing->votes ();
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

std::deque<nano::vote_cache::top_entry> nano::vote_cache::top (const nano::uint128_t & min_tally)
{
	stats.inc (nano::stat::type::vote_cache, nano::stat::detail::top);

	std::deque<top_entry> results;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };

		if (cleanup_interval.elapsed (config.age_cutoff / 2))
		{
			cleanup ();
		}

		for (auto & entry : cache.get<tag_tally> ())
		{
			auto tally = entry.tally ();
			if (tally < min_tally)
			{
				break;
			}
			results.push_back ({ entry.hash (), tally, entry.final_tally () });
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
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto count_unique_votes = [this] () {
		std::unordered_set<std::shared_ptr<nano::vote>> votes;
		for (auto const & entry : cache)
		{
			for (auto const & vote : entry.votes ())
			{
				votes.insert (vote);
			}
		}
		return votes.size ();
	};

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cache", cache.size (), sizeof (ordered_cache::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "unique", count_unique_votes (), sizeof (nano::vote) }));
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
