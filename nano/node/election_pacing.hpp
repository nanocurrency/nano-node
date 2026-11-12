#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>

#include <chrono>
#include <optional>

namespace nano
{
struct election_pacing_params
{
	std::chrono::milliseconds base_latency; // Expected round trip of a vote request, the request interval is a multiple of it by election behavior
	std::chrono::milliseconds vote_interval; // Between broadcasts of this node's own vote
	std::chrono::milliseconds block_interval; // Between floods of the same winner, and between directed broadcasts of it
	std::chrono::milliseconds block_delay; // From a winner's flood until its directed broadcast to the reps that still lack a vote
};

/**
 * Paces outbound election activity: vote broadcasts, winner floods, directed winner broadcasts and confirmation requests.
 * Pure logic with injected time, guarded externally by the owning election's mutex.
 */
class election_pacing final
{
public:
	explicit election_pacing (nano::election_pacing_params params);

	// Vote broadcast is due when the vote interval elapsed since the last one (or none was sent yet)
	bool due_vote (std::chrono::steady_clock::time_point now) const;
	void vote_sent (std::chrono::steady_clock::time_point now);
	// Allow the next vote broadcast immediately
	void reset_vote ();

	// Winner flood is due when none went out yet, the block interval elapsed or the winner changed
	bool due_flood (nano::block_hash const & winner, std::chrono::steady_clock::time_point now) const;
	void flood_sent (nano::block_hash const & winner, std::chrono::steady_clock::time_point now);
	// True until the first flood is recorded
	bool is_first_flood () const;

	// Directed winner broadcast is due once the block delay passed since the winner's latest flood, then again after the block interval or for a new winner
	bool due_directed (nano::block_hash const & winner, std::chrono::steady_clock::time_point now) const;
	void directed_sent (nano::block_hash const & winner, std::chrono::steady_clock::time_point now);

	// Confirmation request is due when the behavior-dependent request interval elapsed
	bool due_request (nano::election_behavior, std::chrono::steady_clock::time_point now) const;
	void request_sent (std::chrono::steady_clock::time_point now);

	// Time between confirmation requests for the given election behavior
	std::chrono::milliseconds request_interval (nano::election_behavior) const;

private:
	nano::election_pacing_params const params;

	std::optional<std::chrono::steady_clock::time_point> last_vote;
	std::optional<std::chrono::steady_clock::time_point> last_flood;
	nano::block_hash last_flood_hash{ 0 };
	std::optional<std::chrono::steady_clock::time_point> last_directed;
	nano::block_hash last_directed_hash{ 0 };
	std::optional<std::chrono::steady_clock::time_point> last_request;
};
}
