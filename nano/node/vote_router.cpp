#include <nano/lib/enum_util.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_router.hpp>

#include <boost/container/flat_set.hpp>
#include <boost/container/static_vector.hpp>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <stdexcept>

using namespace std::chrono_literals;

/*
 * vote_results
 */

nano::vote_results::vote_results (std::shared_ptr<nano::vote> const & vote_a) :
	vote{ vote_a }
{
	release_assert (vote->hashes.size () <= nano::vote::max_hashes);
	codes.resize (vote->hashes.size ());
}

void nano::vote_results::set (size_t position, nano::vote_code code)
{
	codes[position] = code;
	routed.set (position);
}

nano::vote_code nano::vote_results::at (nano::block_hash const & hash) const
{
	for (auto const & entry : entries ())
	{
		if (entry.hash == hash)
		{
			return entry.code;
		}
	}
	throw std::out_of_range ("hash was not routed");
}

size_t nano::vote_results::size () const
{
	return routed.count ();
}

bool nano::vote_results::empty () const
{
	return routed.none ();
}

/*
 * vote_router
 */

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

bool nano::vote_router::connect (nano::block_hash const & hash, std::shared_ptr<nano::election> const & election)
{
	std::unique_lock lock{ mutex };
	auto & by_hash = routes.get<tag_hash> ();
	auto existing = by_hash.find (hash);
	if (existing == by_hash.end ())
	{
		by_hash.insert ({ hash, election });
		return true;
	}
	if (auto current = existing->election.lock ())
	{
		if (current == election)
		{
			return true;
		}
		// Two elections claiming one hash should not happen, the one started later is the current one and keeps the route
		if (current->get_election_start () > election->get_election_start ())
		{
			return false;
		}
	}
	by_hash.modify (existing, [&election] (auto & route) {
		route.election = election;
	});
	return true;
}

void nano::vote_router::disconnect (std::shared_ptr<nano::election> const & election)
{
	std::unique_lock lock{ mutex };
	routes.get<tag_election> ().erase (std::weak_ptr<nano::election>{ election });
}

bool nano::vote_router::disconnect (nano::block_hash const & hash, std::shared_ptr<nano::election> const & election)
{
	std::unique_lock lock{ mutex };
	auto & by_hash = routes.get<tag_hash> ();
	auto existing = by_hash.find (hash);
	if (existing == by_hash.end ())
	{
		return false;
	}
	// Only the election the route points to may remove it
	if (existing->election.owner_before (election) || election.owner_before (existing->election))
	{
		return false;
	}
	by_hash.erase (existing);
	return true;
}

nano::vote_results nano::vote_router::vote (std::shared_ptr<nano::vote> const & vote, nano::vote_source source, nano::block_hash filter)
{
	debug_assert (!vote->validate ()); // false => valid vote
	// If present, filter should be set to one of the hashes in the vote
	debug_assert (filter.is_zero () || std::any_of (vote->hashes.begin (), vote->hashes.end (), [&filter] (auto const & hash) {
		return hash == filter;
	}));

	auto const & hashes = vote->hashes;
	nano::vote_results results{ vote };

	// An election together with the position of the hash it claims, at most one per distinct hash
	struct match
	{
		size_t position;
		std::shared_ptr<nano::election> election;
	};
	boost::container::static_vector<match, nano::vote::max_hashes> matches;

	// Positions ordered by their hash, so inserting a position fails if its hash occurred before
	auto const hash_less = [&hashes] (uint8_t lhs, uint8_t rhs) {
		return hashes[lhs] < hashes[rhs];
	};
	boost::container::flat_set<uint8_t, decltype (hash_less), boost::container::static_vector<uint8_t, nano::vote::max_hashes>> seen{ hash_less };

	auto find_election = [this] (auto const & hash) -> std::shared_ptr<nano::election> {
		auto const & by_hash = routes.get<tag_hash> ();
		if (auto existing = by_hash.find (hash); existing != by_hash.end ())
		{
			return existing->election.lock ();
		}
		return {};
	};

	{
		std::shared_lock lock{ mutex };
		for (size_t position = 0; position < hashes.size (); ++position)
		{
			auto const & hash = hashes[position];

			// Ignore votes for other hashes if a filter is set
			if (!filter.is_zero () && hash != filter)
			{
				continue;
			}

			// Ignore duplicate hashes (should not happen with a well-behaved voting node)
			if (!seen.insert (static_cast<uint8_t> (position)).second)
			{
				continue;
			}

			if (auto election = find_election (hash))
			{
				release_assert (matches.size () < matches.capacity ());
				matches.push_back ({ position, std::move (election) });
			}
			else
			{
				results.set (position, recently_confirmed.contains (hash) ? nano::vote_code::late : nano::vote_code::indeterminate);
			}
		}
	}

	// Announced once, ahead of the first election that gets the vote
	bool const matched = !matches.empty ();
	if (matched)
	{
		vote_matched.notify (vote);
	}

	for (auto const & [position, election] : matches)
	{
		results.set (position, election->vote (vote->account, vote->timestamp (), hashes[position], source));
	}

	// All distinct hashes should have their result set
	debug_assert (!filter.is_zero () || results.size () == seen.size ());

	// Cache the votes that didn't match any election
	if (source != nano::vote_source::cache)
	{
		vote_cache.insert (vote, results);

		// An election that started since the lookup may have read the cache before the insert, so look up the unmatched hashes once more
		auto const unmatched = [] (auto const & entry) {
			return entry.code == nano::vote_code::indeterminate;
		};
		matches.clear ();
		if (std::ranges::any_of (results.entries (), unmatched))
		{
			std::shared_lock lock{ mutex };
			for (auto const & entry : results.entries () | std::views::filter (unmatched))
			{
				if (auto election = find_election (entry.hash))
				{
					release_assert (matches.size () < matches.capacity ());
					matches.push_back ({ entry.position, std::move (election) });
				}
			}
		}
		if (!matched && !matches.empty ())
		{
			vote_matched.notify (vote);
		}
		for (auto const & [position, election] : matches)
		{
			// Any other result means the election got the vote from the cache or is over already
			if (election->vote (vote->account, vote->timestamp (), hashes[position], source) == nano::vote_code::vote)
			{
				results.set (position, nano::vote_code::vote);
			}
		}
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
