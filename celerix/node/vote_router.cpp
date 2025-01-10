#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/vote_cache.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/secure/vote.hpp>

#include <chrono>

using namespace std::chrono_literals;

celerix::stat::detail celerix::to_stat_detail (celerix::vote_code code)
{
	return celerix::enum_util::cast<celerix::stat::detail> (code);
}

celerix::stat::detail celerix::to_stat_detail (celerix::vote_source source)
{
	return celerix::enum_util::cast<celerix::stat::detail> (source);
}

celerix::vote_router::vote_router (celerix::vote_cache & vote_cache_a, celerix::recently_confirmed_cache & recently_confirmed_a) :
	vote_cache{ vote_cache_a },
	recently_confirmed{ recently_confirmed_a }
{
}

celerix::vote_router::~vote_router ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void celerix::vote_router::connect (celerix::block_hash const & hash, std::weak_ptr<celerix::election> election)
{
	std::unique_lock lock{ mutex };
	elections.insert_or_assign (hash, election);
}

void celerix::vote_router::disconnect (celerix::election const & election)
{
	std::unique_lock lock{ mutex };
	for (auto const & [hash, _] : election.blocks ())
	{
		elections.erase (hash);
	}
}

void celerix::vote_router::disconnect (celerix::block_hash const & hash)
{
	std::unique_lock lock{ mutex };
	[[maybe_unused]] auto erased = elections.erase (hash);
	debug_assert (erased == 1);
}

// Validate a vote and apply it to the current election if one exists
std::unordered_map<celerix::block_hash, celerix::vote_code> celerix::vote_router::vote (std::shared_ptr<celerix::vote> const & vote, celerix::vote_source source, celerix::block_hash filter)
{
	debug_assert (!vote->validate ()); // false => valid vote
	// If present, filter should be set to one of the hashes in the vote
	debug_assert (filter.is_zero () || std::any_of (vote->hashes.begin (), vote->hashes.end (), [&filter] (auto const & hash) {
		return hash == filter;
	}));

	std::unordered_map<celerix::block_hash, celerix::vote_code> results;
	std::unordered_map<celerix::block_hash, std::shared_ptr<celerix::election>> process;
	{
		std::shared_lock lock{ mutex };
		for (auto const & hash : vote->hashes)
		{
			// Ignore votes for other hashes if a filter is set
			if (!filter.is_zero () && hash != filter)
			{
				continue;
			}

			// Ignore duplicate hashes (should not happen with a well-behaved voting node)
			if (results.find (hash) != results.end ())
			{
				continue;
			}

			auto find_election = [this] (auto const & hash) -> std::shared_ptr<celerix::election> {
				if (auto existing = elections.find (hash); existing != elections.end ())
				{
					return existing->second.lock ();
				}
				return {};
			};

			if (auto election = find_election (hash))
			{
				process[hash] = election;
			}
			else
			{
				if (!recently_confirmed.exists (hash))
				{
					results[hash] = celerix::vote_code::indeterminate;
				}
				else
				{
					results[hash] = celerix::vote_code::replay;
				}
			}
		}
	}

	for (auto const & [block_hash, election] : process)
	{
		auto const vote_result = election->vote (vote->account, vote->timestamp (), block_hash, source);
		results[block_hash] = vote_result;
	}

	// All hashes should have their result set
	debug_assert (!filter.is_zero () || std::all_of (vote->hashes.begin (), vote->hashes.end (), [&results] (auto const & hash) {
		return results.find (hash) != results.end ();
	}));

	// Cache the votes that didn't match any election
	if (source != celerix::vote_source::cache)
	{
		vote_cache.insert (vote, results);
	}

	vote_processed.notify (vote, source, results);

	return results;
}

bool celerix::vote_router::active (celerix::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	if (auto existing = elections.find (hash); existing != elections.end ())
	{
		if (auto election = existing->second.lock (); election != nullptr)
		{
			return true;
		}
	}
	return false;
}

std::shared_ptr<celerix::election> celerix::vote_router::election (celerix::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	if (auto existing = elections.find (hash); existing != elections.end ())
	{
		if (auto election = existing->second.lock (); election != nullptr)
		{
			return election;
		}
	}
	return nullptr;
}

// This is meant to be a fast check and may return false positives if weak pointers have expired, but we don't care about that here
bool celerix::vote_router::contains (celerix::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	return elections.contains (hash);
}

void celerix::vote_router::start ()
{
	thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::vote_router);
		run ();
	} };
}

void celerix::vote_router::stop ()
{
	std::unique_lock lock{ mutex };
	stopped = true;
	lock.unlock ();
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void celerix::vote_router::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		std::erase_if (elections, [] (auto const & pair) { return pair.second.lock () == nullptr; });
		condition.wait_for (lock, 15s, [&] () { return stopped; });
	}
}

celerix::container_info celerix::vote_router::container_info () const
{
	std::shared_lock lock{ mutex };

	celerix::container_info info;
	info.put ("elections", elections);
	return info;
}
