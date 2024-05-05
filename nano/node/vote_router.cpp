#include <nano/lib/enum_util.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_router.hpp>

#include <chrono>

using namespace std::chrono_literals;

nano::stat::detail nano::to_stat_detail (nano::vote_code code)
{
	return nano::enum_util::cast<nano::stat::detail> (code);
}

nano::stat::detail nano::to_stat_detail (nano::vote_source source)
{
	return nano::enum_util::cast<nano::stat::detail> (source);
}

nano::vote_router::vote_router (nano::vote_cache & cache, nano::recently_confirmed_cache & recently_confirmed) :
	cache{ cache },
	recently_confirmed{ recently_confirmed }
{
}

nano::vote_router::~vote_router ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::vote_router::connect (nano::block_hash const & hash, std::weak_ptr<nano::election> election)
{
	std::unique_lock lock{ mutex };
	elections.insert_or_assign (hash, election);
}

void nano::vote_router::disconnect (nano::election const & election)
{
	std::unique_lock lock{ mutex };
	for (auto const & [hash, _] : election.blocks ())
	{
		elections.erase (hash);
	}
}

void nano::vote_router::disconnect (nano::block_hash const & hash)
{
	std::unique_lock lock{ mutex };
	[[maybe_unused]] auto erased = elections.erase (hash);
	debug_assert (erased == 1);
}

// Validate a vote and apply it to the current election if one exists
std::unordered_map<nano::block_hash, nano::vote_code> nano::vote_router::vote (std::shared_ptr<nano::vote> const & vote, nano::vote_source source, nano::block_hash filter)
{
	debug_assert (!vote->validate ()); // false => valid vote
	// If present, filter should be set to one of the hashes in the vote
	debug_assert (filter.is_zero () || std::any_of (vote->hashes.begin (), vote->hashes.end (), [&filter] (auto const & hash) {
		return hash == filter;
	}));

	std::unordered_map<nano::block_hash, nano::vote_code> results;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> process;
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

			auto find_election = [this] (auto const & hash) {
				if (auto existing = elections.find (hash); existing != elections.end ())
				{
					return existing->second.lock ();
				}
				return std::shared_ptr<nano::election>{};
			};

			if (auto election = find_election (hash))
			{
				process[hash] = election;
			}
			else
			{
				if (!recently_confirmed.exists (hash))
				{
					results[hash] = nano::vote_code::indeterminate;
				}
				else
				{
					results[hash] = nano::vote_code::replay;
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

	vote_processed.notify (vote, source, results);

	return results;
}

bool nano::vote_router::active (nano::block_hash const & hash) const
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

std::shared_ptr<nano::election> nano::vote_router::election (nano::block_hash const & hash) const
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

void nano::vote_router::start ()
{
	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::vote_router);
		run ();
	} };
}

void nano::vote_router::stop ()
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

void nano::vote_router::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		std::erase_if (elections, [] (auto const & pair) { return pair.second.lock () == nullptr; });
		condition.wait_for (lock, 15s, [&] () { return stopped; });
	}
}

nano::container_info nano::vote_router::container_info () const
{
	std::shared_lock lock{ mutex };

	nano::container_info info;
	info.put ("elections", elections);
	return info;
}