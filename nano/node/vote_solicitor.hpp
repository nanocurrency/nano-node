#pragma once

#include <nano/lib/random.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/secure/vote.hpp>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <unordered_map>
#include <utility>

namespace nano
{
class vote_solicitor_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	// Relay peers asked per election loop round, zero disables relay requests
	std::size_t relay_fanout{ 1 };
	// Hard cap on representatives per relay request, must not exceed the cap relays accept
	std::size_t relay_reps{ 32 };
	// Slots for representatives seen voting recently, heaviest first
	std::size_t relay_reps_observed{ 28 };
	// Slots for representatives not seen voting, rotated so every one gets asked
	std::size_t relay_reps_explore{ 4 };
};

/** Caps for the representatives asked through relays in one round, by selection strategy */
struct relay_rep_limits final
{
	std::size_t max_reps{ 32 }; // Reps per request, the strategies fill up to it in order
	std::size_t observed{ 28 }; // Reps seen voting recently, heaviest first
	std::size_t explore{ 4 }; // Reps not seen voting, least recently asked first
};

/**
 * Picks the unreachable representatives to ask through relays in a round.
 * Observed reps rank by weight so the ones deciding quorum are asked first, unobserved reps rotate by the time they were last asked so every one gets its turn.
 * Pure logic with injected time, the vote_solicitor owns it.
 */
class relay_rep_selector final
{
public:
	struct candidate
	{
		nano::account account;
		nano::uint128_t weight;
		bool observed; // Seen voting within the online window
	};

	explicit relay_rep_selector (relay_rep_limits const &);

	// Pick the reps for a round and record them as asked
	std::deque<nano::account> select (std::deque<candidate> const &, std::chrono::steady_clock::time_point now);

private:
	relay_rep_limits const limits;
	// When each candidate was last asked, trimmed to the current candidates on every selection
	std::unordered_map<nano::account, std::chrono::steady_clock::time_point> last_asked;
};

/** Who a solicitation round can ask through relays */
struct relay_targets final
{
	std::deque<nano::account> reps; // Principal representatives without a channel
	std::deque<std::shared_ptr<nano::transport::channel>> relays; // Relay peers accepting requests this round
};

/** Elections, by winner hash, that each kind of message actually went out for, the election loop stamps their pacing with it */
struct solicitation_result final
{
	boost::unordered_flat_set<nano::block_hash> block_broadcasts; // The winner reached at least one of its target channels or had none to reach
	boost::unordered_flat_set<nano::block_hash> vote_requests; // At least one confirm_req carrying the election went out
	boost::unordered_flat_set<nano::block_hash> relay_requests; // A relay request carrying the election went out
};

/**
 * Plans one solicitation round: which representatives to ask for votes, directly or through a relay, and which to send the winner block to, batched per channel.
 * Reps are grouped by channel, a channel gets one message per election and counts towards the fork voters' caps when a rep behind it voted for a different hash, towards the others otherwise.
 * Vote requests go heaviest first. A winner broadcast goes to channels picked at random, up to one cap among fork voters and another among the reps without a vote, so the nodes of the network spread their copies over every rep.
 * Each channel is asked for room once when the round starts and then takes at most channel_messages of each traffic type.
 * Elections missing the same reps without a channel share one relay request.
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

	// A relay request shared by the elections missing the same reps
	struct relay_batch
	{
		std::deque<nano::account> reps; // Reps that still need to answer
		nano::vote_finality finality; // Before quorum any vote settles a rep, after it only a final one does
		roots_hashes_t roots_hashes;
	};

	using vote_requests_t = boost::unordered_flat_map<std::shared_ptr<nano::transport::channel>, roots_hashes_t>;
	using block_broadcasts_t = std::deque<block_broadcast>;
	using relay_requests_t = std::deque<relay_batch>;

	// What a round decided against while planning, the solicitor counts it once the round is sent
	struct planning_counts
	{
		std::size_t requested_elections{ 0 }; // Elections at least one vote request was planned for
		std::size_t full_channels{ 0 }; // Channels at max for a traffic type when the round started, left alone for the round
		std::size_t exhausted{ 0 }; // Channels skipped because their budget for the round was spent
		std::size_t broadcasts_without_targets{ 0 }; // Winner broadcasts no rep needed
	};

public:
	// The seed draws the random broadcast targets, a node draws a fresh one for every round
	vote_solicitor_round (std::deque<nano::representative> const & reps, nano::solicitation_constants const &, uint64_t seed = 0, nano::relay_targets = {});

	// Plan vote requests for an election, heaviest first, up to one cap among fork voters and another among the reps without a vote; a channel without budget left is skipped
	// @return true if any request was planned
	bool request_votes (nano::election_snapshot const &);
	// Plan a directed winner broadcast for an election: random fork voters' channels up to the fork cap and random channels without a vote up to the other cap; a channel without budget left is skipped
	// @return false if every rep needing the block is behind a channel without budget, a broadcast no rep needs is planned without channels
	bool broadcast_block (nano::election_snapshot const &);
	// Plan a relay request for an election, covering the reps without a channel that still need to answer
	// @return true if a request was planned
	bool relay_request (nano::election_snapshot const &);

	// Planned confirm_req payloads per channel
	vote_requests_t const & vote_requests () const;
	// Planned winner broadcasts with their target channels
	block_broadcasts_t const & block_broadcasts () const;
	// Planned relay requests, one per set of reps and finality
	relay_requests_t relay_requests () const;
	// Reps without a channel the round may ask through relays
	std::deque<nano::account> const & relay_reps () const;
	// Relay peers the round may send to
	channels_t const & relays () const;
	// What planning decided against so far
	planning_counts const & counts () const;

private:
	// Per representative decision, a rep that voted for a different hash counts towards the fork voters' caps, ordered so a channel takes the strongest of its reps
	enum class rep_decision
	{
		skip,
		solicit,
		solicit_fork,
	};

	// Whether a vote request to the rep is warranted: no vote, a non-final vote after quorum, or a non-final vote for a different hash
	static rep_decision decide_vote_request (nano::election_snapshot const &, nano::account const & rep);
	// Whether a winner broadcast to the rep is warranted: no vote or a non-final vote for a different hash
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

	// A channel picked for an election while planning, the entry stays owned by the round
	using channel_ref = std::reference_wrapper<channel_entry>;

	// Strongest decision among the reps behind the channel
	template <typename Decide>
	static rep_decision decide_channel (nano::election_snapshot const &, channel_entry const &, Decide const &);

private:
	nano::solicitation_constants const limits;

	std::deque<channel_entry> channels_m; // Heaviest rep first, budgets spent as the round plans
	std::deque<nano::account> const relay_reps_m;
	channels_t const relays_m;
	std::mt19937_64 rng_m; // Picks the random broadcast targets
	planning_counts counts_m;

	vote_requests_t vote_requests_m;
	block_broadcasts_t block_broadcasts_m;
	// Hashes keyed by the reps still to answer and the finality wanted, elections in the same situation share a request
	std::map<std::pair<std::deque<nano::account>, nano::vote_finality>, roots_hashes_t> relay_requests_m;
};

/**
 * Solicits votes for active elections on behalf of the election loop.
 * Prepares rounds against the reachable principal representatives, the principal representatives without a channel worth asking through relays, and the relay peers to ask.
 * Sends the requests, relay requests and directed winner broadcasts a round planned.
 */
class vote_solicitor final
{
public:
	vote_solicitor (nano::vote_solicitor_config const &, nano::rep_crawler &, nano::rep_tiers &, nano::online_reps &, nano::ledger &, nano::vote_relay_client &, nano::network_params const &, nano::stats &, nano::logger &);

	// Start a round against the currently reachable principal representatives and relays
	nano::vote_solicitor_round prepare ();
	// Send everything the round planned
	// @return what went out, a message a channel or the relay client refused leaves its election due again next round
	nano::solicitation_result flush (nano::vote_solicitor_round const &);

private:
	// Principal representatives without a channel, with their weight and whether they were seen voting recently
	std::deque<nano::relay_rep_selector::candidate> relay_candidates (std::deque<nano::representative> const & direct) const;

private: // Dependencies
	nano::vote_solicitor_config const & config;
	nano::rep_crawler & rep_crawler;
	nano::rep_tiers & rep_tiers;
	nano::online_reps & online_reps;
	nano::ledger & ledger;
	nano::vote_relay_client & vote_relay_client;
	nano::network_params const & network_params;
	nano::stats & stats;
	nano::logger & logger;

private:
	nano::random_generator_mt random; // Seeds every round
	nano::relay_rep_selector selector; // Rotates the unobserved reps asked through relays across rounds
	std::mutex mutex; // Guards the selector, rounds are normally prepared from the election loop only
};
}
