#include <nano/lib/enum_util.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_context.hpp>
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
	auto const [existing, inserted] = routes.try_emplace (hash, election);
	if (inserted)
	{
		return true;
	}
	if (auto current = existing->second.lock ())
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
	existing->second = election;
	return true;
}

void nano::vote_router::disconnect (std::shared_ptr<nano::election> const & election)
{
	std::unique_lock lock{ mutex };
	// A sweep of the whole table, once per election that ends, which stays cheap for a table the size of the active elections
	erase_if (routes, [&election] (auto const & route) {
		return !route.second.owner_before (election) && !election.owner_before (route.second);
	});
}

bool nano::vote_router::disconnect (nano::block_hash const & hash, std::shared_ptr<nano::election> const & election)
{
	std::unique_lock lock{ mutex };
	auto existing = routes.find (hash);
	if (existing == routes.end ())
	{
		return false;
	}
	// Only the election the route points to may remove it
	if (existing->second.owner_before (election) || election.owner_before (existing->second))
	{
		return false;
	}
	routes.erase (existing);
	return true;
}

nano::vote_results nano::vote_router::vote (nano::vote_context const & context, nano::block_hash filter)
{
	auto const & vote = context.vote;
	debug_assert (!vote->validate ()); // false => valid vote
	// If present, filter should be set to one of the hashes in the vote
	debug_assert (filter.is_zero () || std::any_of (vote->hashes.begin (), vote->hashes.end (), [&filter] (auto const & hash) {
		return hash == filter;
	}));

	// Working state for one vote, sized by the hash limit so none of it allocates
	auto const & hashes = vote->hashes;
	nano::vote_results results{ vote };

	// Only votes of principal representatives reach elections
	bool const principal = context.principal;

	// An election together with the position of the hash it claims, at most one per distinct hash
	struct match
	{
		size_t position;
		std::shared_ptr<nano::election> election;
	};
	boost::container::static_vector<match, nano::vote::max_hashes> matches;

	// Hashes seen so far, held as positions into `hashes` and compared by hash: inserting a position whose hash occurred before fails, and 255 one-byte positions need no heap
	auto const hash_less = [&hashes] (uint8_t lhs, uint8_t rhs) {
		return hashes[lhs] < hashes[rhs];
	};
	boost::container::flat_set<uint8_t, decltype (hash_less), boost::container::static_vector<uint8_t, nano::vote::max_hashes>> seen{ hash_less };

	auto find_election = [this] (auto const & hash) -> std::shared_ptr<nano::election> {
		if (auto existing = routes.find (hash); existing != routes.end ())
		{
			return existing->second.lock ();
		}
		return {};
	};

	// Under one lock, match each distinct hash to a live election or answer it right away as late or unknown
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

			// Ignore duplicate hashes, which fail to insert since the set compares positions by hash (should not happen with a well-behaved voting node)
			if (!seen.insert (static_cast<uint8_t> (position)).second)
			{
				continue;
			}

			if (auto election = principal ? find_election (hash) : nullptr)
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

	// Hand the vote to the matched elections
	// Announced once, ahead of the first election that gets the vote
	bool const matched = !matches.empty ();
	if (matched)
	{
		vote_matched.notify (context);
	}

	// Each election answers for its hash: the vote is counted, a replay, ignored under cooldown, or late or unknown once the election is over
	for (auto const & [position, election] : matches)
	{
		results.set (position, election->vote (vote->account, vote->timestamp (), hashes[position], context.source, context.tier));
	}

	// All distinct hashes should have their result set
	debug_assert (!filter.is_zero () || results.size () == seen.size ());

	// Cache a vote that did not come from the cache itself, then route its unmatched hashes once more
	if (context.source != nano::vote_source::cache)
	{
		// Caching votes of untiered representatives would let anyone fill the cache
		if (context.tier != nano::rep_tier::none)
		{
			vote_cache.insert (vote, context.weight, results);
		}

		// An election that started since the lookup may have read the cache before the insert, so look up the unmatched hashes once more
		auto const unmatched = [] (auto const & entry) {
			return entry.code == nano::vote_code::indeterminate;
		};
		matches.clear ();
		if (principal && std::ranges::any_of (results.entries (), unmatched))
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
		// Announced now if the first lookup matched nothing
		if (!matched && !matches.empty ())
		{
			vote_matched.notify (context);
		}
		for (auto const & [position, election] : matches)
		{
			// Any other result means the election got the vote from the cache or is over already
			if (election->vote (vote->account, vote->timestamp (), hashes[position], context.source, context.tier) == nano::vote_code::vote)
			{
				results.set (position, nano::vote_code::vote);
			}
		}
	}

	// Report the vote with its code per hash to the observers, the rebroadcaster among them
	vote_processed.notify (context, results);

	return results;
}

bool nano::vote_router::active (nano::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	if (auto existing = routes.find (hash); existing != routes.end ())
	{
		return !existing->second.expired ();
	}
	return false;
}

std::shared_ptr<nano::election> nano::vote_router::election (nano::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	if (auto existing = routes.find (hash); existing != routes.end ())
	{
		return existing->second.lock ();
	}
	return nullptr;
}

bool nano::vote_router::contains (nano::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	return routes.contains (hash);
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
		erase_if (routes, [] (auto const & route) {
			return route.second.expired ();
		});
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
