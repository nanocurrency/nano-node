#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/fwd.hpp>

#include <condition_variable>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace nano
{
enum class vote_code
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest timestamp, it's a replay
	vote, // Vote has the highest timestamp
	indeterminate, // Unknown if replay or vote
	ignored, // Vote is valid, but got ingored (e.g. due to cooldown)
	late, // Vote is late, the election is already confirmed and present in the recently confirmed set
};

nano::stat::detail to_stat_detail (vote_code);
std::string_view to_string (vote_code);

enum class vote_source
{
	live,
	rebroadcast,
	cache,
};

nano::stat::detail to_stat_detail (vote_source);
std::string_view to_string (vote_source);

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
	 * Existing routes will be replaced.
	 * Election must hold the block for the hash being passed in.
	 */
	void connect (nano::block_hash const & hash, std::weak_ptr<nano::election> election);
	/**
	 * Remove all routes to this election.
	 * @return number of routes removed
	 */
	std::size_t disconnect (nano::election const & election);
	/**
	 * Remove route for hash.
	 * @return true if route existed and was removed
	 */
	bool disconnect (nano::block_hash const & hash);

	/**
	 * Route vote to associated elections.
	 * Distinguishes replay votes, cannot be determined if the block is not in any election.
	 * If 'filter' parameter is non-zero, only elections for the specified hash are notified.
	 * This eliminates duplicate processing when triggering votes from the vote_cache as the result of a specific election being created.
	 */
	std::unordered_map<nano::block_hash, nano::vote_code> vote (std::shared_ptr<nano::vote> const &, nano::vote_source = nano::vote_source::live, nano::block_hash filter = { 0 });

	bool active (nano::block_hash const & hash) const;
	std::shared_ptr<nano::election> election (nano::block_hash const & hash) const;
	bool contains (nano::block_hash const & hash) const;

	nano::container_info container_info () const;

public: // Events
	using vote_processed_event_t = nano::observer_set<std::shared_ptr<nano::vote> const &, nano::vote_source, std::unordered_map<nano::block_hash, nano::vote_code> const &>;
	vote_processed_event_t vote_processed;

private: // Dependencies
	nano::vote_cache & vote_cache;
	nano::recently_confirmed_cache & recently_confirmed;

private:
	void run ();

	// Mapping of block hashes to elections
	std::unordered_map<nano::block_hash, std::weak_ptr<nano::election>> elections;

	bool stopped{ false };
	mutable std::shared_mutex mutex;
	std::condition_variable_any condition;
	std::thread thread;
};
}
