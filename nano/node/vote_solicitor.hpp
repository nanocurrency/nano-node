#pragma once

#include <nano/node/fwd.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/secure/network_params.hpp>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <deque>
#include <memory>
#include <utility>

namespace nano
{
/** Elections, by winner hash, that each kind of message actually went out for, the election loop stamps their pacing with it */
struct solicitation_result final
{
	boost::unordered_flat_set<nano::block_hash> block_broadcasts; // The winner reached at least one of its target channels or had none to reach
	boost::unordered_flat_set<nano::block_hash> vote_requests; // At least one confirm_req carrying the election went out
};

/**
 * Plans one solicitation round: which representatives to ask for votes and which to send the winner block to, batched per channel.
 * Reps are grouped by channel, a channel gets one message per election and counts towards the per election caps unless a rep behind it voted for a different hash.
 * Each channel is asked for room once when the round starts and then takes at most channel_messages of each traffic type.
 * Pure bookkeeping without I/O, the vote_solicitor sends what a round accumulates.
 */
class vote_solicitor_round final
{
public:
	using roots_hashes_t = std::deque<std::pair<nano::block_hash, nano::root>>;
	using channels_t = std::deque<std::shared_ptr<nano::transport::channel>>;

	// A winner block and the channels to send it to
	struct block_broadcast
	{
		std::shared_ptr<nano::block> block;
		channels_t channels; // Empty when no rep needs the block
	};

	using vote_requests_t = boost::unordered_flat_map<std::shared_ptr<nano::transport::channel>, roots_hashes_t>;
	using block_broadcasts_t = std::deque<block_broadcast>;

	// What a round decided against while planning, the solicitor counts it once the round is sent
	struct planning_counts
	{
		std::size_t requested_elections{ 0 }; // Elections at least one vote request was planned for
		std::size_t full_channels{ 0 }; // Channels at max for a traffic type when the round started, left alone for the round
		std::size_t exhausted{ 0 }; // Channels skipped because their budget for the round was spent
		std::size_t broadcasts_without_targets{ 0 }; // Winner broadcasts no rep needed
	};

public:
	vote_solicitor_round (std::deque<nano::representative> const & reps, nano::solicitation_constants const &);

	// Plan vote requests for an election, a channel without budget left is skipped without taking a cap slot, fork voters are reached past the cap
	// @return true if any request was planned
	bool request_votes (nano::election_snapshot const &);
	// Plan a directed winner broadcast for an election, a channel without budget left is skipped without taking a cap slot, fork voters are reached past the cap
	// @return false if every rep needing the block is behind a channel without budget, a broadcast no rep needs is planned without channels
	bool broadcast_block (nano::election_snapshot const &);

	// Planned confirm_req payloads per channel
	vote_requests_t const & vote_requests () const;
	// Planned winner broadcasts with their target channels
	block_broadcasts_t const & block_broadcasts () const;
	// What planning decided against so far
	planning_counts const & counts () const;

private:
	// Per representative decision, a rep that voted for a different hash is exempt from the per election caps, ordered so a channel takes the strongest of its reps
	enum class rep_decision
	{
		skip,
		solicit,
		solicit_fork,
	};

	// Whether a vote request to the rep is warranted: no vote, a non-final vote after quorum, or a vote for a different hash
	static rep_decision decide_vote_request (nano::election_snapshot const &, nano::account const & rep);
	// Whether a winner broadcast to the rep is warranted: no vote or a vote for a different hash
	static rep_decision decide_block_broadcast (nano::election_snapshot const &, nano::account const & rep);

	// Messages the round may still plan to a channel, per traffic type
	struct channel_budget
	{
		std::size_t requests;
		std::size_t broadcasts;
	};

	// A channel with the reps behind it, in the order of its heaviest rep
	struct channel_entry
	{
		std::shared_ptr<nano::transport::channel> channel;
		std::deque<nano::account> reps;
		channel_budget budget; // Set from the channel's room once when the round starts
	};

	// Strongest decision among the reps behind the channel
	template <typename Decide>
	static rep_decision decide_channel (nano::election_snapshot const &, channel_entry const &, Decide const &);

private:
	nano::solicitation_constants const limits;

	std::deque<channel_entry> channels_m; // Heaviest rep first, budgets spent as the round plans
	planning_counts counts_m;

	vote_requests_t vote_requests_m;
	block_broadcasts_t block_broadcasts_m;
};

/**
 * Solicits votes for active elections on behalf of the election loop.
 * Prepares rounds against the reachable principal representatives and sends the requests and directed winner broadcasts a round planned.
 */
class vote_solicitor final
{
public:
	vote_solicitor (nano::rep_crawler &, nano::network_params const &, nano::stats &, nano::logger &);

	// Start a round against the currently reachable principal representatives
	nano::vote_solicitor_round prepare ();
	// Send everything the round planned
	// @return what went out, a message a channel refused leaves its election due again next round
	nano::solicitation_result flush (nano::vote_solicitor_round const &);

private: // Dependencies
	nano::rep_crawler & rep_crawler;
	nano::network_params const & network_params;
	nano::stats & stats;
	nano::logger & logger;
};
}
