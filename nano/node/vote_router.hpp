#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/node/fwd.hpp>

#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace celerix
{
enum class vote_code
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest timestamp, it's a replay
	vote, // Vote has the highest timestamp
	indeterminate, // Unknown if replay or vote
	ignored, // Vote is valid, but got ingored (e.g. due to cooldown)
};

celerix::stat::detail to_stat_detail (vote_code);

enum class vote_source
{
	live,
	rebroadcast,
	cache,
};

celerix::stat::detail to_stat_detail (vote_source);

// This class routes votes to their associated election
// This class holds a weak_ptr as this container does not own the elections
// Routing entries are removed periodically if the weak_ptr has expired
class vote_router final
{
public:
	vote_router (celerix::vote_cache & cache, celerix::recently_confirmed_cache & recently_confirmed);
	~vote_router ();

	void start ();
	void stop ();

	// Add a route for 'hash' to 'election'
	// Existing routes will be replaced
	// Election must hold the block for the hash being passed in
	void connect (celerix::block_hash const & hash, std::weak_ptr<celerix::election> election);
	// Remove all routes to this election
	void disconnect (celerix::election const & election);
	void disconnect (celerix::block_hash const & hash);
	// Route vote to associated elections
	// Distinguishes replay votes, cannot be determined if the block is not in any election

	// If 'filter' parameter is non-zero, only elections for the specified hash are notified.
	// This eliminates duplicate processing when triggering votes from the vote_cache as the result of a specific election being created.
	std::unordered_map<celerix::block_hash, celerix::vote_code> vote (std::shared_ptr<celerix::vote> const &, celerix::vote_source = celerix::vote_source::live, celerix::block_hash filter = { 0 });
	bool active (celerix::block_hash const & hash) const;
	std::shared_ptr<celerix::election> election (celerix::block_hash const & hash) const;
	bool contains (celerix::block_hash const & hash) const;

	using vote_processed_event_t = celerix::observer_set<std::shared_ptr<celerix::vote> const &, celerix::vote_source, std::unordered_map<celerix::block_hash, celerix::vote_code> const &>;
	vote_processed_event_t vote_processed;

	celerix::container_info container_info () const;

private: // Dependencies
	celerix::vote_cache & vote_cache;
	celerix::recently_confirmed_cache & recently_confirmed;

private:
	void run ();

private:
	// Mapping of block hashes to elections.
	// Election already contains the associated block
	std::unordered_map<celerix::block_hash, std::weak_ptr<celerix::election>> elections;

	bool stopped{ false };
	std::condition_variable_any condition;
	mutable std::shared_mutex mutex;
	std::thread thread;
};
}
