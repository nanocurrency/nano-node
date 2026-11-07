#pragma once

#include <nano/node/election.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/repcrawler.hpp>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <deque>
#include <memory>
#include <utility>

namespace nano
{
/** Caps applied when planning a round */
struct solicitation_limits final
{
	std::size_t election_vote_requests{ 50 }; // Vote requests per election, a rep that voted for a different hash is exempt
	std::size_t election_block_broadcasts{ 15 }; // Directed winner broadcasts per election, fork voters are exempt likewise
	std::size_t round_block_broadcasts{ 30 }; // Winner broadcasts per round across all elections
};

/** Elections, by winner hash, that each kind of message actually went out for, the election loop stamps their pacing with it */
struct solicitation_result final
{
	boost::unordered_flat_set<nano::block_hash> block_broadcasts; // The winner reached at least one of its target channels or had none to reach
	boost::unordered_flat_set<nano::block_hash> vote_requests; // At least one confirm_req carrying the election went out
};

/**
 * Plans one solicitation round: which representatives to ask for votes and which to send the winner block to, batched per channel.
 * Pure bookkeeping without I/O, the vote_solicitor sends what a round accumulates.
 */
class vote_solicitor_round final
{
public:
	// Per representative decision, a rep that voted for a different hash is exempt from the per election caps
	enum class rep_decision
	{
		skip,
		solicit,
		solicit_fork,
	};

	using roots_hashes_t = std::deque<std::pair<nano::block_hash, nano::root>>;
	using channels_t = std::deque<std::shared_ptr<nano::transport::channel>>;

public:
	vote_solicitor_round (std::deque<nano::representative> reps, nano::solicitation_limits const &);

	// Whether a vote request to the rep is warranted: no vote, a non-final vote after quorum, or a vote for a different hash
	static rep_decision decide_vote_request (nano::election_snapshot const &, nano::account const & rep);
	// Whether a winner broadcast to the rep is warranted: no vote or a vote for a different hash
	static rep_decision decide_block_broadcast (nano::election_snapshot const &, nano::account const & rep);

	// Plan vote requests to the reps for an election, a rep behind a full channel is dropped for the rest of the round
	// @return true if any request was planned
	bool request_votes (nano::election_snapshot const &);
	// Plan a directed winner broadcast for an election
	// @return false if the round's broadcast cap is reached
	bool broadcast_block (nano::election_snapshot const &);

	// Planned confirm_req payloads per channel
	boost::unordered_flat_map<std::shared_ptr<nano::transport::channel>, roots_hashes_t> const & vote_requests () const;
	// Planned winner broadcasts with their target channels
	std::deque<std::pair<std::shared_ptr<nano::block>, channels_t>> const & block_broadcasts () const;
	// Number of elections vote requests were planned for
	std::size_t requested_elections () const;

private:
	nano::solicitation_limits const limits;

	std::deque<nano::representative> vote_request_reps; // Reps with a full channel are dropped for the rest of the round
	std::deque<nano::representative> const block_broadcast_reps;

	std::size_t block_broadcast_count{ 0 };
	std::size_t requested_elections_count{ 0 };

	boost::unordered_flat_map<std::shared_ptr<nano::transport::channel>, roots_hashes_t> vote_requests_m;
	std::deque<std::pair<std::shared_ptr<nano::block>, channels_t>> block_broadcasts_m;
};

/**
 * Solicits votes for active elections on behalf of the election loop.
 * Prepares rounds against the reachable principal representatives and sends the requests and directed winner broadcasts a round planned.
 */
class vote_solicitor final
{
public:
	vote_solicitor (nano::network &, nano::rep_crawler &, nano::network_constants const &, nano::stats &);

	// Start a round against the currently reachable principal representatives
	nano::vote_solicitor_round prepare ();
	// Send everything the round planned
	// @return what went out, a message a channel refused leaves its election due again next round
	nano::solicitation_result flush (nano::vote_solicitor_round const &);

private: // Dependencies
	nano::network & network;
	nano::rep_crawler & rep_crawler;
	nano::network_constants const & network_constants;
	nano::stats & stats;

private:
	// Caps for a round, the per election broadcast cap follows the current network fanout
	nano::solicitation_limits round_limits () const;
};
}
