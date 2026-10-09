#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/fwd.hpp>

#include <boost/container/static_vector.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <bitset>
#include <condition_variable>
#include <memory>
#include <ranges>
#include <shared_mutex>
#include <thread>

namespace mi = boost::multi_index;

namespace nano
{
enum class vote_code : uint8_t
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest timestamp, it's a replay
	vote, // Vote has the highest timestamp
	indeterminate, // Unknown if replay or vote
	ignored, // Vote is valid, but got ignored (e.g. due to cooldown)
	late, // Vote is late, the election is already confirmed and present in the recently confirmed set
};

nano::stat::detail to_stat_detail (vote_code);
std::string_view to_string (vote_code);

enum class vote_source : uint8_t
{
	live,
	rebroadcast,
	cache,
};

nano::stat::detail to_stat_detail (vote_source);
std::string_view to_string (vote_source);

/**
 * What the vote router did with the hashes of one vote.
 * Holds one code per position in the vote's list of hashes and no hashes of its own, so it lives on the stack whatever the size of the vote.
 * Positions that were not routed have no code: hashes excluded by a filter and repeats of an earlier hash.
 */
class vote_results final
{
public:
	// A routed hash of the vote
	struct entry final
	{
		size_t position; // Index of the hash within the vote
		nano::block_hash const & hash;
		nano::vote_code code;
	};

	explicit vote_results (std::shared_ptr<nano::vote> const &);

	// Records what happened to the hash at `position`, replacing an earlier code
	void set (size_t position, nano::vote_code);

	// Code of a routed hash, throws std::out_of_range for any other hash
	nano::vote_code at (nano::block_hash const &) const;

	// Number of routed hashes
	size_t size () const;
	bool empty () const;

	// Routed hashes in the order of the vote
	auto entries () const
	{
		return std::views::iota (size_t{ 0 }, codes.size ())
		| std::views::filter ([this] (size_t position) { return routed.test (position); })
		| std::views::transform ([this] (size_t position) { return entry{ position, vote->hashes[position], codes[position] }; });
	}

private:
	std::shared_ptr<nano::vote> vote;
	boost::container::static_vector<nano::vote_code, nano::vote::max_hashes> codes; // One slot per hash of the vote
	std::bitset<nano::vote::max_hashes> routed; // Positions that hold a code
};

/**
 * Routes votes to their associated elections.
 * Holds weak_ptr to elections as this container does not own them.
 * Routing entries are removed periodically if the weak_ptr has expired.
 */
class vote_router final
{
public:
	vote_router (nano::vote_cache &, nano::recently_confirmed_cache &);
	~vote_router ();

	void start ();
	void stop ();

	/**
	 * Add a route for 'hash' to 'election'.
	 * A hash belongs to a single election, so an existing route to another election is replaced unless that election started later.
	 * The hash must belong to the election's root, which the router cannot verify.
	 * Connect before reading the vote cache for 'hash', votes that arrive in between are then delivered by the router.
	 * @return true if the route points to 'election' afterwards
	 */
	bool connect (nano::block_hash const & hash, std::shared_ptr<nano::election> const & election);
	/**
	 * Remove all routes to this election.
	 */
	void disconnect (std::shared_ptr<nano::election> const & election);
	/**
	 * Remove the route for 'hash' if it points to 'election'.
	 * @return true if the route was removed
	 */
	bool disconnect (nano::block_hash const & hash, std::shared_ptr<nano::election> const & election);

	/**
	 * Route vote to associated elections.
	 * Distinguishes replay votes, cannot be determined if the block is not in any election.
	 * A vote for a hash without a route is cached, then the route is looked up once more in case an election connected meanwhile.
	 * If 'filter' parameter is non-zero, only elections for the specified hash are notified.
	 * This eliminates duplicate processing when triggering votes from the vote_cache as the result of a specific election being created.
	 * The context carries what the vote processor established about the vote, which the elections and the cache read instead of looking it up.
	 */
	nano::vote_results vote (nano::vote_context const &, nano::block_hash filter = { 0 });

	bool active (nano::block_hash const & hash) const;
	std::shared_ptr<nano::election> election (nano::block_hash const & hash) const;
	bool contains (nano::block_hash const & hash) const;

	nano::container_info container_info () const;

public: // Events
	// Notified once for a vote with at least one hash held by a live election, before the vote reaches any of them
	using vote_matched_event_t = nano::observer_set<nano::vote_context const &>;
	vote_matched_event_t vote_matched;

	using vote_processed_event_t = nano::observer_set<nano::vote_context const &, nano::vote_results const &>;
	vote_processed_event_t vote_processed;

private: // Dependencies
	nano::vote_cache & vote_cache;
	nano::recently_confirmed_cache & recently_confirmed;

private:
	void run ();

	struct route
	{
		nano::block_hash hash;
		std::weak_ptr<nano::election> election;
	};

	// clang-format off
	class tag_hash {};
	class tag_election {};

	using route_index = mi::multi_index_container<route,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<route, nano::block_hash, &route::hash>>,
		mi::ordered_non_unique<mi::tag<tag_election>,
			mi::member<route, std::weak_ptr<nano::election>, &route::election>,
			std::owner_less<std::weak_ptr<nano::election>>>>
	>;
	// clang-format on
	// Routes indexed by both block hash and election ownership
	route_index routes;

	bool stopped{ false };
	mutable std::shared_mutex mutex;
	std::condition_variable_any condition;
	std::thread thread;
};
}
