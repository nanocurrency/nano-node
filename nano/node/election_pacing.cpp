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

bool nano::election_pacing::due_block (nano::block_hash const & winner, std::chrono::steady_clock::time_point now) const
{
	if (!last_block || *last_block + params.block_interval < now)
	{
		return true;
	}
	if (winner != last_block_hash)
	{
		return true;
	}
	return false;
}

void nano::election_pacing::block_sent (nano::block_hash const & winner, std::chrono::steady_clock::time_point now)
{
	last_block = now;
	last_block_hash = winner;
}

bool nano::election_pacing::is_first_block () const
{
	return !last_block.has_value ();
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
