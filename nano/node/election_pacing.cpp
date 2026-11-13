#include <nano/lib/constants.hpp>
#include <nano/node/election_pacing.hpp>

#include <algorithm>

nano::election_pacing::election_pacing (nano::election_pacing_params params_a) :
	params{ params_a }
{
}

nano::election_pacing::election_pacing (nano::network_constants const & network) :
	election_pacing{ {
	.base_latency = network.election_base_latency,
	.vote_interval = network.vote_broadcast_interval,
	.flood_interval = network.block_flood_interval,
	.block_interval = network.block_broadcast_interval,
	.block_delay = network.block_broadcast_delay,
	.request_interval = network.election_request_interval,
	} }
{
}

bool nano::election_pacing::due_vote (std::chrono::steady_clock::time_point now) const
{
	return !last_vote || now >= *last_vote + params.vote_interval;
}

void nano::election_pacing::vote_sent (std::chrono::steady_clock::time_point now)
{
	last_vote = now;
	++vote_count;
}

void nano::election_pacing::reset_vote ()
{
	last_vote.reset ();
}

bool nano::election_pacing::due_flood (nano::block_hash const & winner, std::chrono::steady_clock::time_point now) const
{
	return !last_flood || *last_flood + params.flood_interval < now || winner != last_flood_hash;
}

void nano::election_pacing::flood_sent (nano::block_hash const & winner, std::chrono::steady_clock::time_point now)
{
	last_flood = now;
	last_flood_hash = winner;
	++flood_count;
}

bool nano::election_pacing::is_first_flood () const
{
	return flood_count == 0;
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
	++directed_count;
}

bool nano::election_pacing::due_request (std::chrono::steady_clock::time_point now) const
{
	return !last_request || request_interval () < now - *last_request;
}

void nano::election_pacing::request_sent (std::chrono::steady_clock::time_point now)
{
	last_request = now;
	++request_count;
}

std::chrono::milliseconds nano::election_pacing::request_interval () const
{
	// One doubling per request after the first, bounded so the shift of a long running election cannot overflow
	auto const doublings = request_count > 0 ? std::min (request_count - 1, 31u) : 0u;
	return std::min (params.base_latency * (1u << doublings), params.request_interval);
}
