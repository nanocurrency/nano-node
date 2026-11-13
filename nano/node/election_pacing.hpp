#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>

#include <chrono>
#include <optional>

namespace nano
{
struct election_pacing_params
{
	std::chrono::milliseconds base_latency; // Expected round trip of a vote request, the first gap between vote requests
	std::chrono::milliseconds vote_interval; // Between broadcasts of this node's own vote
	std::chrono::milliseconds flood_interval; // Between floods of the same winner
	std::chrono::milliseconds block_interval; // Between directed broadcasts of the same winner
	std::chrono::milliseconds block_delay; // From a winner's flood until its directed broadcast to the reps that still lack a vote
	std::chrono::milliseconds request_interval; // Longest gap between vote requests, the gap doubles up to it from the base latency
};

/**
 * Paces outbound election activity: vote broadcasts, winner floods, directed winner broadcasts and confirmation requests.
 * Pure logic with injected time, guarded externally by the owning election's mutex.
 */
class election_pacing final
{
public:
	explicit election_pacing (nano::election_pacing_params params);
	// Paces by the election timings of the network
	explicit election_pacing (nano::network_constants const &);

	// Vote broadcast is due when the vote interval elapsed since the last one (or none was sent yet)
	bool due_vote (std::chrono::steady_clock::time_point now) const;
	void vote_sent (std::chrono::steady_clock::time_point now);
	// Allow the next vote broadcast immediately
	void reset_vote ();

	// Winner flood is due when none went out yet, the flood interval elapsed or the winner changed
	bool due_flood (nano::block_hash const & winner, std::chrono::steady_clock::time_point now) const;
	void flood_sent (nano::block_hash const & winner, std::chrono::steady_clock::time_point now);
	// True until the first flood is recorded
	bool is_first_flood () const;

	// Directed winner broadcast is due once the block delay passed since the winner's latest flood, then again after the block interval or for a new winner
	bool due_directed (nano::block_hash const & winner, std::chrono::steady_clock::time_point now) const;
	void directed_sent (nano::block_hash const & winner, std::chrono::steady_clock::time_point now);

	// Confirmation request is due when none went out yet or the request interval elapsed since the latest one
	bool due_request (std::chrono::steady_clock::time_point now) const;
	// Record a confirmation request that went out, each one doubles the gap to the next up to the configured request interval
	void request_sent (std::chrono::steady_clock::time_point now);

	// Time from the latest confirmation request until the next one, between the base latency and the configured request interval
	std::chrono::milliseconds request_interval () const;

private:
	nano::election_pacing_params const params;

	// Latest time and number of each action recorded as sent, with the winner of the latest block action
	std::optional<std::chrono::steady_clock::time_point> last_vote;
	unsigned vote_count{ 0 };
	std::optional<std::chrono::steady_clock::time_point> last_flood;
	nano::block_hash last_flood_hash{ 0 };
	unsigned flood_count{ 0 };
	std::optional<std::chrono::steady_clock::time_point> last_directed;
	nano::block_hash last_directed_hash{ 0 };
	unsigned directed_count{ 0 };
	std::optional<std::chrono::steady_clock::time_point> last_request;
	unsigned request_count{ 0 };
};
}
