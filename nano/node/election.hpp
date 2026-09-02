#pragma once

#include <nano/lib/id_dispenser.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/election_pacing.hpp>
#include <nano/node/election_status.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/election_ballot.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace nano
{
/** Point-in-time view of election state needed to solicit votes and broadcast the winner */
struct election_snapshot final
{
	nano::qualified_root qualified_root;
	std::shared_ptr<nano::block> winner;
	bool quorum; // Vote quorum was reached, only final votes are of interest
	std::unordered_map<nano::account, nano::vote_info> votes;
};

/** Outbound actions requested by an election when ticked, performed by the active elections loop */
struct election_actions final
{
	// Snapshot of the election state, present whenever a block broadcast or vote request is due
	std::optional<nano::election_snapshot> snapshot;
	// Broadcast the current winner block
	bool broadcast_block{ false };
	// Solicit votes from representatives
	bool request_votes{ false };
	// Election is finished and should be erased from the active set
	bool cleanup{ false };
};

enum class election_state
{
	passive, // only listening for incoming votes
	active, // actively request confirmations
	confirmed, // decided; the record is sealed, the winner block may still be re-broadcast
	expired_confirmed,
	expired_unconfirmed,
	cancelled,
};

std::string_view to_string (election_state);
nano::stat::detail to_stat_detail (election_state);

class election final : public std::enable_shared_from_this<election>
{
	nano::id_t const id{ nano::next_id () }; // Track individual objects when tracing

private:
	// Minimum time between broadcasts of the current winner of an election, as a backup to requesting confirmations
	std::chrono::milliseconds base_latency () const;

	// Callbacks
	std::function<void (std::shared_ptr<nano::block> const &)> confirmation_action;
	std::function<void (nano::account const &)> vote_action;
	std::function<void (nano::qualified_root const &)> update_action;

private: // State management
	static unsigned constexpr passive_duration_factor = 5;
	nano::election_state state_m{ election_state::passive };

	std::chrono::steady_clock::time_point state_start{ std::chrono::steady_clock::now () };

	bool valid_change (nano::election_state, nano::election_state) const;
	bool state_change (nano::election_state, nano::election_state);

public: // State transitions
	// Advance the election state machine and return the outbound actions that are due
	nano::election_actions tick (std::chrono::steady_clock::time_point now);

	// Broadcast a vote for the current winner via the vote generator, if one is due
	void broadcast_vote ();

	// Record a successful confirmation request round
	void request_sent ();
	// Record a successful broadcast of the given winner block
	void broadcast_sent (nano::block_hash const & winner);

	bool transition_active ();
	bool transition_priority ();

	// Outcome of a requested state change
	struct transition_result final
	{
		nano::election_state previous; // State observed before the request
		nano::election_state current; // State after the request, equal to previous when nothing changed
	};

	// Cancel a live election and report the states observed under the lock; a sealed election is left as it is
	transition_result cancel ();

public: // Status
	bool confirmed () const;
	bool failed () const;
	std::shared_ptr<nano::block> winner () const;
	std::chrono::milliseconds duration () const;

	// Copy of the current status, composed under the mutex
	nano::election_status get_status () const;
	// The status joined with copies of the ballot's votes, blocks and tally
	nano::election_extended_status get_extended_status () const;

	std::atomic<unsigned> confirmation_request_count{ 0 };
	std::atomic<unsigned> vote_broadcast_count{ 0 };

	nano::tally_map tally () const;

public: // Interface
	election (
	nano::node &,
	std::shared_ptr<nano::block> const & block,
	nano::election_behavior behavior,
	nano::bucket_index bucket = 0,
	std::function<void (std::shared_ptr<nano::block> const &)> confirmation_action = nullptr,
	std::function<void (nano::account const &)> vote_action = nullptr,
	std::function<void (nano::qualified_root const &)> update_action = nullptr);

	std::shared_ptr<nano::block> find (nano::block_hash const &) const;

	/*
	 * Process vote. Internally uses cooldown to throttle non-final votes
	 * If the election reaches consensus, it will be confirmed
	 */
	nano::vote_code vote (nano::account const & representative, uint64_t timestamp, nano::block_hash const & block_hash, nano::vote_source source);

	// Submit a competing fork block; returns whether it was newly admitted to the ballot
	bool publish (std::shared_ptr<nano::block> const & block);

	std::chrono::steady_clock::time_point get_election_start () const
	{
		return election_start;
	}
	std::chrono::steady_clock::time_point get_state_start () const
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		return state_start;
	}

private: // Dependencies
	nano::node & node;

private:
	// Paces outbound votes, winner block broadcasts and confirmation requests
	nano::election_pacing pacing;
	// Vote and block bookkeeping, owns the current winner
	nano::election_ballot ballot;

public: // Information
	uint64_t const height;
	nano::root const root;
	nano::qualified_root const qualified_root;
	nano::account const account;
	nano::bucket_index const bucket;

	std::vector<nano::vote_with_weight_info> votes_with_weight () const;
	nano::election_behavior behavior () const;
	nano::election_state state () const;

	// All recorded votes by representative
	std::unordered_map<nano::account, nano::vote_info> votes () const;
	// All held blocks by hash
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks () const;
	// Hashes of the held blocks
	std::unordered_set<nano::block_hash> blocks_hashes () const;
	// Whether a block with the given hash is held
	bool contains_block (nano::block_hash const &) const;

	size_t voter_count () const;
	size_t block_count () const;

private:
	// Retally the ballot against the current quorum threshold and handle a winner switch
	nano::election_ballot::round evaluate_locked ();
	// Confirm the current winner once final vote quorum is met
	void confirm_if_quorum (nano::unique_lock<nano::mutex> &);
	// Snapshot for vote solicitation and winner broadcasts
	nano::election_snapshot snapshot_locked () const;
	// Whether the election reached confirmation, including the expired-confirmed end state
	bool confirmed_locked () const;
	// Whether the election reached a terminal state: the record is sealed and the ballot no longer changes
	bool sealed_locked () const;
	// Compose the current status from the ballot, the single place where the public status copy is derived
	nano::election_status status_locked () const;
	// Extend the composed status with copies of the ballot's votes, blocks and tally
	nano::election_extended_status extended_status_locked () const;
	// Transition into the confirmed state and dispatch the confirmation actions; the lock does not own the mutex on return
	void confirm_once (nano::unique_lock<nano::mutex> & lock);
	// Broadcast a vote for the current winner if due, final if reached quorum or already confirmed
	void broadcast_vote_locked (std::chrono::steady_clock::time_point now);
	// Maximum lifetime by election behavior before the election expires
	std::chrono::milliseconds time_to_live () const;

private:
	std::atomic<bool> is_quorum{ false };
	nano::election_behavior behavior_m;
	std::chrono::steady_clock::time_point const election_start{ std::chrono::steady_clock::now () };

	// Result of the last ballot evaluation; the winner anchors switch detection, the tallies keep status reads cheap, and it becomes the frozen round of a sealed election
	nano::election_ballot::round last_round;

	// Wall-clock time the election reached confirmation, unset while unconfirmed
	std::chrono::system_clock::time_point election_end{};

	// Time from election start to confirmation, unset while unconfirmed
	std::chrono::milliseconds election_duration{};

	mutable nano::mutex mutex;

public: // Logging
	void operator() (nano::object_stream &) const;

public: // Only used in tests
	void force_confirm ();
};
}
