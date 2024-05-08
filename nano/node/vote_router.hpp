#pragma once

#include <nano/lib/numbers.hpp>

#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace nano
{
class container_info_component;
class election;
class recently_confirmed_cache;
class vote;
class vote_cache;
}

namespace nano
{
enum class vote_code
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest timestamp, it's a replay
	vote, // Vote has the highest timestamp
	indeterminate, // Unknown if replay or vote
	ignored, // Vote is valid, but got ingored (e.g. due to cooldown)
};

nano::stat::detail to_stat_detail (vote_code);

enum class vote_source
{
	live,
	cache,
};

nano::stat::detail to_stat_detail (vote_source);

// This class routes votes to their associated election
// This class holds a weak_ptr as this container does not own the elections
// Routing entries are removed perodically if the weak_ptr has expired
class vote_router final
{
public:
	vote_router (nano::vote_cache & cache, nano::recently_confirmed_cache & recently_confirmed);
	~vote_router ();
	// Add a route for 'hash' to 'election'
	// Existing routes will be replaced
	// Election must hold the block for the hash being passed in
	void connect (nano::block_hash const & hash, std::weak_ptr<nano::election> election);
	// Remove all routes to this election
	void disconnect (nano::election const & election);
	void disconnect (nano::block_hash const & hash);
	// Route vote to associated elections
	// Distinguishes replay votes, cannot be determined if the block is not in any election
	std::unordered_map<nano::block_hash, nano::vote_code> vote (std::shared_ptr<nano::vote> const &, nano::vote_source = nano::vote_source::live);
	bool trigger_vote_cache (nano::block_hash const & hash);
	bool active (nano::block_hash const & hash) const;
	std::shared_ptr<nano::election> election (nano::block_hash const & hash) const;

	void start ();
	void stop ();

	using vote_processed_event_t = nano::observer_set<std::shared_ptr<nano::vote> const &, nano::vote_source, std::unordered_map<nano::block_hash, nano::vote_code> const &>;
	vote_processed_event_t vote_processed;

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

private:
	void run ();

	nano::vote_cache & cache;
	nano::recently_confirmed_cache & recently_confirmed;
	// Mapping of block hashes to elections.
	// Election already contains the associated block
	std::unordered_map<nano::block_hash, std::weak_ptr<nano::election>> elections;
	bool stopped{ false };
	std::condition_variable_any condition;
	mutable std::shared_mutex mutex;
	std::thread thread;
};
}
