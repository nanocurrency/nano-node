#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/election.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/repcrawler.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

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

/** Who a solicitation round can send to */
struct solicitation_targets final
{
	std::vector<nano::representative> direct; // Principal representatives with a channel, asked with confirm_req and sent the winner block
	std::deque<nano::account> relayed; // Principal representatives without a channel, asked through relays
	std::deque<std::shared_ptr<nano::transport::channel>> relays; // Relay peers accepting requests this round
};

/** Caps applied when planning a round */
struct solicitation_limits final
{
	std::size_t election_vote_requests{ 50 }; // Vote requests per election, a rep that voted for a different hash is exempt
	std::size_t election_block_broadcasts{ 15 }; // Directed winner broadcasts per election, forks exempt likewise
	std::size_t round_block_broadcasts{ 30 }; // Winner broadcasts per round across all elections
};

/**
 * Plans one solicitation round: which representatives to ask for votes, directly or through a relay, and which to send the winner block to, batched per channel.
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

	// Whether a vote request to the rep is warranted: no vote, a non-final vote after quorum, or a vote for a different hash
	static rep_decision decide_vote_request (nano::election_snapshot const &, nano::account const & rep);
	// Whether a winner broadcast to the rep is warranted: no vote or a vote for a different hash
	static rep_decision decide_block_broadcast (nano::election_snapshot const &, nano::account const & rep);

	using roots_hashes_t = std::deque<std::pair<nano::block_hash, nano::root>>;
	using channels_t = std::deque<std::shared_ptr<nano::transport::channel>>;

	// A relay request shared by the elections missing the same reps
	struct relay_batch
	{
		std::deque<nano::account> reps; // Reps that still need to answer
		bool include_non_final; // Before quorum any vote settles a rep, after it only a final one does
		roots_hashes_t roots_hashes;
	};

	vote_solicitor_round (nano::solicitation_targets, nano::solicitation_limits const &);

	// Plan vote requests to the reachable reps for an election
	// @return true if any request was planned
	bool request_votes (nano::election_snapshot const &);
	// Plan a directed winner broadcast for an election
	// @return false if the round's broadcast cap is reached
	bool broadcast_block (nano::election_snapshot const &);
	// Plan a relay request for an election, covering the unreachable reps that still need to answer
	// @return true if a request was planned
	bool relay_request (nano::election_snapshot const &);

	// Planned confirm_req payloads per channel
	std::unordered_map<std::shared_ptr<nano::transport::channel>, roots_hashes_t> const & vote_requests () const;
	// Planned winner broadcasts with their target channels
	std::vector<std::pair<std::shared_ptr<nano::block>, channels_t>> const & block_broadcasts () const;
	// Planned relay requests
	std::deque<relay_batch> relay_requests () const;

	// Unreachable reps the round may ask through relays
	std::deque<nano::account> const & relay_reps () const;
	// Relay peers the round may send to
	channels_t const & relays () const;

	// Queried before planning a request on a channel, a full channel is left alone for the rest of the round
	std::function<bool (std::shared_ptr<nano::transport::channel> const &)> channel_full{ default_channel_full };

private:
	static bool default_channel_full (std::shared_ptr<nano::transport::channel> const &);

	nano::solicitation_limits const limits;

	std::vector<nano::representative> vote_request_reps; // Reps with a full channel are dropped for the rest of the round
	std::vector<nano::representative> const block_broadcast_reps;
	std::deque<nano::account> const relay_reps_m;
	channels_t const relays_m;

	std::size_t block_broadcast_count{ 0 };

	std::unordered_map<std::shared_ptr<nano::transport::channel>, roots_hashes_t> vote_requests_m;
	std::vector<std::pair<std::shared_ptr<nano::block>, channels_t>> block_broadcasts_m;
	// Hashes keyed by the reps still to answer and the finality wanted, elections in the same situation share a request
	std::map<std::pair<std::deque<nano::account>, bool>, roots_hashes_t> relay_requests_m;
};

/**
 * Solicits votes for active elections on behalf of the election loop.
 * Builds the targets for a round: the reachable representatives, the unreachable ones worth asking through relays, and the relay peers to ask.
 * Sends the requests, relay requests and directed winner broadcasts a round planned.
 */
class vote_solicitor final
{
public:
	vote_solicitor (vote_solicitor_config const &, nano::network &, nano::rep_crawler &, nano::rep_tiers &, nano::online_reps &, nano::ledger &, nano::vote_relay_client &, nano::network_constants const &, nano::stats &, nano::logger &);

	// Start a round against the currently reachable representatives and relays
	nano::vote_solicitor_round prepare ();
	// Send everything the round planned
	void flush (nano::vote_solicitor_round const &);

	// Caps for a round, the per election broadcast cap is derived from the network fanout at each prepare
	nano::solicitation_limits const limits;

private:
	// Principal representatives without a channel, with their weight and whether they were seen voting recently
	std::deque<nano::relay_rep_selector::candidate> relay_candidates (std::vector<nano::representative> const & direct) const;

private: // Dependencies
	vote_solicitor_config const & config;
	nano::network & network;
	nano::rep_crawler & rep_crawler;
	nano::rep_tiers & rep_tiers;
	nano::online_reps & online_reps;
	nano::ledger & ledger;
	nano::vote_relay_client & vote_relay_client;
	nano::network_constants const & network_constants;
	nano::stats & stats;
	nano::logger & logger;

private:
	nano::relay_rep_selector selector;
	// Guards the selector, rounds are normally prepared from the election loop only
	nano::mutex mutex;
};
}
