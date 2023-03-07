#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/election_insertion_result.hpp>

#include <memory>
#include <unordered_set>

namespace nano
{
class active_transactions;
class block;
class election;
enum class election_behavior;
class stats;
/**
	This class is a facade around active_transactions that limits the number of elections that can be inserted.
*/
class election_occupancy : public std::enable_shared_from_this<election_occupancy>
{
public:
	election_occupancy (nano::active_transactions & active, size_t limit, nano::election_behavior behavior);
	// Checks whether there is availability to insert an election for 'block' and if so, spawns a new election
	nano::election_insertion_result activate (std::shared_ptr<nano::block> const & block);
	// Returns whether there is availability to insert a new election
	bool available () const;
	// Returns the upper limit on the number of elections allowed to be started
	size_t limit () const;
	std::unordered_set<nano::qualified_root> elections () const;

private:
	size_t election_destruction_notification (nano::qualified_root const & root);

	nano::active_transactions & active;
	size_t const limit_m;
	nano::election_behavior behavior;
	// Tracks the elections that have been started through this facade
	std::unordered_set<nano::qualified_root> elections_m;
	std::function<nano::election_insertion_result (std::shared_ptr<nano::block> block)> start_election;

	mutable nano::mutex mutex;
};
} // namespace nano
