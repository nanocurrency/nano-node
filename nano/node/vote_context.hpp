#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/secure/rep_tiers.hpp>

#include <memory>

namespace nano
{
/**
 * A vote together with what the node established about it when it was queued, looked up once by the vote processor and read along the way.
 */
struct vote_context final
{
	std::shared_ptr<nano::vote> vote;
	nano::vote_source source; // Where the vote came from
	std::shared_ptr<nano::transport::channel> channel; // Peer that delivered the vote, null for local and cached votes
	nano::rep_tier tier; // Representative tier when the vote was queued
	nano::uint128_t weight; // Representative weight when the vote was queued, it may have changed since, so no consensus decision may rest on it
	bool principal; // Whether the representative held principal weight when the vote was queued, always true on the dev network
};
}
