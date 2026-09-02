#include <nano/lib/enum_util.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_router.hpp>

#include <chrono>

using namespace std::chrono_literals;

nano::vote_router::vote_router (nano::vote_cache & vote_cache_a, nano::recently_confirmed_cache & recently_confirmed_a) :
	vote_cache{ vote_cache_a },
	recently_confirmed{ recently_confirmed_a }
{
}

nano::vote_router::~vote_router ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
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
	{
		std::unique_lock lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void nano::vote_router::connect (nano::block_hash const & hash, std::shared_ptr<nano::election> const & election)
{
	std::unique_lock lock{ mutex };
	auto & by_hash = routes.get<tag_hash> ();
	if (auto existing = by_hash.find (hash); existing != by_hash.end ())
	{
		by_hash.modify (existing, [&election] (auto & route) {
			route.election = election;
		});
	}
	else
	{
		by_hash.insert ({ hash, election });
	}
}

void nano::vote_router::disconnect (std::shared_ptr<nano::election> const & election)
{
	std::unique_lock lock{ mutex };
	routes.get<tag_election> ().erase (std::weak_ptr<nano::election>{ election });
}

bool nano::vote_router::disconnect (nano::block_hash const & hash)
{
	std::unique_lock lock{ mutex };
	auto erased = routes.get<tag_hash> ().erase (hash);
	return erased > 0;
}

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

			auto find_election = [this] (auto const & hash) -> std::shared_ptr<nano::election> {
				auto const & by_hash = routes.get<tag_hash> ();
				if (auto existing = by_hash.find (hash); existing != by_hash.end ())
				{
					return existing->election.lock ();
				}
				return {};
			};

			if (auto election = find_election (hash))
			{
				process[hash] = election;
			}
			else
			{
				if (recently_confirmed.contains (hash))
				{
					results[hash] = nano::vote_code::late;
				}
				else
				{
					results[hash] = nano::vote_code::indeterminate;
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
	if (source != nano::vote_source::cache)
	{
		vote_cache.insert (vote, results);
	}

	vote_processed.notify (vote, source, results);

	return results;
}

bool nano::vote_router::active (nano::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	auto const & by_hash = routes.get<tag_hash> ();
	if (auto existing = by_hash.find (hash); existing != by_hash.end ())
	{
		if (auto election = existing->election.lock (); election != nullptr)
		{
			return true;
		}
	}
	return false;
}

std::shared_ptr<nano::election> nano::vote_router::election (nano::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	auto const & by_hash = routes.get<tag_hash> ();
	if (auto existing = by_hash.find (hash); existing != by_hash.end ())
	{
		if (auto election = existing->election.lock (); election != nullptr)
		{
			return election;
		}
	}
	return nullptr;
}

bool nano::vote_router::contains (nano::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	return routes.get<tag_hash> ().contains (hash);
}

nano::container_info nano::vote_router::container_info () const
{
	std::shared_lock lock{ mutex };

	nano::container_info info;
	info.put ("routes", routes);
	return info;
}

void nano::vote_router::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		auto & by_hash = routes.get<tag_hash> ();
		for (auto it = by_hash.begin (); it != by_hash.end ();)
		{
			if (it->election.expired ())
			{
				it = by_hash.erase (it);
			}
			else
			{
				++it;
			}
		}
		condition.wait_for (lock, 15s, [&] () { return stopped; });
	}
}

/*
 *
 */

nano::stat::detail nano::to_stat_detail (nano::vote_code code)
{
	return nano::enum_convert<nano::stat::detail> (code);
}

std::string_view nano::to_string (nano::vote_code code)
{
	return nano::enum_to_string (code);
}

nano::stat::detail nano::to_stat_detail (nano::vote_source source)
{
	return nano::enum_convert<nano::stat::detail> (source);
}

std::string_view nano::to_string (nano::vote_source source)
{
	return nano::enum_to_string (source);
}
