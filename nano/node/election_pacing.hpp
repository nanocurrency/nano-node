#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>

#include <chrono>
#include <optional>

namespace nano
{
struct election_pacing_params
{
	std::chrono::milliseconds base_latency;
	std::chrono::milliseconds vote_interval;
	std::chrono::milliseconds block_interval;
};

/**
 * Paces outbound election activity: vote broadcasts, winner block broadcasts and confirmation requests.
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

	// Winner block broadcast is due when the block interval elapsed or the winner changed
	bool due_block (nano::block_hash const & winner, std::chrono::steady_clock::time_point now) const;
	void block_sent (nano::block_hash const & winner, std::chrono::steady_clock::time_point now);
	// True until the first block broadcast is recorded
	bool is_first_block () const;

	// Confirmation request is due when the behavior-dependent request interval elapsed
	bool due_request (nano::election_behavior, std::chrono::steady_clock::time_point now) const;
	void request_sent (std::chrono::steady_clock::time_point now);

	// Time between confirmation requests for the given election behavior
	std::chrono::milliseconds request_interval (nano::election_behavior) const;

private:
	nano::election_pacing_params const params;

	std::optional<std::chrono::steady_clock::time_point> last_vote;
	std::optional<std::chrono::steady_clock::time_point> last_block;
	std::optional<std::chrono::steady_clock::time_point> last_request;
	nano::block_hash last_block_hash{ 0 };
};
}
