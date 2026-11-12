#include <nano/lib/assert.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/election_pacing.hpp>

nano::election_pacing::election_pacing (nano::election_pacing_params params_a) :
	params{ params_a }
{
}

bool nano::election_pacing::due_vote (std::chrono::steady_clock::time_point now) const
{
	return !last_vote || now >= *last_vote + params.vote_interval;
}

void nano::election_pacing::vote_sent (std::chrono::steady_clock::time_point now)
{
	last_vote = now;
}

void nano::election_pacing::reset_vote ()
{
	last_vote.reset ();
}

bool nano::election_pacing::due_flood (nano::block_hash const & winner, std::chrono::steady_clock::time_point now) const
{
	return !last_flood || *last_flood + params.block_interval < now || winner != last_flood_hash;
}

void nano::election_pacing::flood_sent (nano::block_hash const & winner, std::chrono::steady_clock::time_point now)
{
	last_flood = now;
	last_flood_hash = winner;
}

bool nano::election_pacing::is_first_flood () const
{
	return !last_flood.has_value ();
}

bool nano::election_pacing::due_directed (nano::block_hash const & winner, std::chrono::steady_clock::time_point now) const
{
	// The winner's flood goes first, the reps that still lack a vote get it directly once the flood had time to spread
	if (!last_flood || winner != last_flood_hash || now < *last_flood + params.block_delay)
	{
		return false;
	}
	return !last_directed || *last_directed + params.block_interval < now || winner != last_directed_hash;
}

void nano::election_pacing::directed_sent (nano::block_hash const & winner, std::chrono::steady_clock::time_point now)
{
	last_directed = now;
	last_directed_hash = winner;
}

bool nano::election_pacing::due_request (nano::election_behavior behavior, std::chrono::steady_clock::time_point now) const
{
	return !last_request || request_interval (behavior) < now - *last_request;
}

void nano::election_pacing::request_sent (std::chrono::steady_clock::time_point now)
{
	last_request = now;
}

std::chrono::milliseconds nano::election_pacing::request_interval (nano::election_behavior behavior) const
{
	switch (behavior)
	{
		case nano::election_behavior::manual:
		case nano::election_behavior::priority:
		case nano::election_behavior::hinted:
			return params.base_latency * 5;
		case nano::election_behavior::optimistic:
			return params.base_latency * 2;
	}
	debug_assert (false);
	return {};
}
