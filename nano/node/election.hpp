#pragma once

#include <nano/lib/id_dispenser.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/election_status.hpp>
#include <nano/node/vote_with_weight_info.hpp>

#include <atomic>
#include <chrono>
#include <memory>

namespace nano
{
class block;
class channel;
class confirmation_solicitor;
class inactive_cache_information;
class node;

class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t timestamp;
	nano::block_hash hash;
};

nano::stat::detail to_stat_detail (nano::election_behavior);

// map of vote weight per block, ordered greater first
using tally_t = std::map<nano::uint128_t, std::shared_ptr<nano::block>, std::greater<nano::uint128_t>>;

struct election_extended_status final
{
	nano::election_status status;
	std::unordered_map<nano::account, nano::vote_info> votes;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks;
	nano::tally_t tally;

	void operator() (nano::object_stream &) const;
};

class election final : public std::enable_shared_from_this<nano::election>
{
	nano::id_t const id{ nano::next_id () }; // Track individual objects when tracing

private:
	// Minimum time between broadcasts of the current winner of an election, as a backup to requesting confirmations
	std::chrono::milliseconds base_latency () const;
	std::function<void (std::shared_ptr<nano::block> const &)> confirmation_action;
	std::function<void (nano::account const &)> live_vote_action;

private: // State management
	enum class state_t
	{
		passive, // only listening for incoming votes
		active, // actively request confirmations
		confirmed, // confirmed but still listening for votes
		expired_confirmed,
		expired_unconfirmed
	};

	static unsigned constexpr passive_duration_factor = 5;
	static unsigned constexpr active_request_count_min = 2;
	nano::election::state_t state_m = { state_t::passive };

	std::chrono::steady_clock::duration state_start{ std::chrono::steady_clock::now ().time_since_epoch () };

	// These are modified while not holding the mutex from transition_time only
	std::chrono::steady_clock::time_point last_block{};
	nano::block_hash last_block_hash{ 0 };
	std::chrono::steady_clock::time_point last_req{};
	/** The last time vote for this election was generated */
	std::chrono::steady_clock::time_point last_vote{};

	bool valid_change (nano::election::state_t, nano::election::state_t) const;
	bool state_change (nano::election::state_t, nano::election::state_t);

public: // State transitions
	bool transition_time (nano::confirmation_solicitor &);
	void transition_active ();

public: // Status
	bool confirmed () const;
	bool failed () const;
	nano::election_extended_status current_status () const;
	std::shared_ptr<nano::block> winner () const;
	std::atomic<unsigned> confirmation_request_count{ 0 };

	nano::tally_t tally () const;
	bool have_quorum (nano::tally_t const &) const;

	// Guarded by mutex
	nano::election_status status;

public: // Interface
	election (nano::node &, std::shared_ptr<nano::block> const & block, std::function<void (std::shared_ptr<nano::block> const &)> const & confirmation_action, std::function<void (nano::account const &)> const & vote_action, nano::election_behavior behavior);

	std::shared_ptr<nano::block> find (nano::block_hash const &) const;
	/*
	 * Process vote. Internally uses cooldown to throttle non-final votes
	 * If the election reaches consensus, it will be confirmed
	 */
	nano::vote_code vote (nano::account const & representative, uint64_t timestamp, nano::block_hash const & block_hash, nano::vote_source = nano::vote_source::live);
	bool publish (std::shared_ptr<nano::block> const & block_a);
	// Confirm this block if quorum is met
	void confirm_if_quorum (nano::unique_lock<nano::mutex> &);
	void try_confirm (nano::block_hash const & hash);

	/**
	 * Broadcasts vote for the current winner of this election
	 * Checks if sufficient amount of time (`vote_generation_interval`) passed since the last vote generation
	 */
	void broadcast_vote ();
	nano::vote_info get_last_vote (nano::account const & account);
	void set_last_vote (nano::account const & account, nano::vote_info vote_info);
	nano::election_status get_status () const;

private: // Dependencies
	nano::node & node;

public: // Information
	uint64_t const height;
	nano::root const root;
	nano::qualified_root const qualified_root;
	std::vector<nano::vote_with_weight_info> votes_with_weight () const;
	nano::election_behavior behavior () const;

private:
	nano::tally_t tally_impl () const;
	bool confirmed_locked () const;
	nano::election_extended_status current_status_locked () const;
	// lock_a does not own the mutex on return
	void confirm_once (nano::unique_lock<nano::mutex> & lock_a);
	bool broadcast_block_predicate () const;
	void broadcast_block (nano::confirmation_solicitor &);
	void send_confirm_req (nano::confirmation_solicitor &);
	/**
	 * Broadcast vote for current election winner. Generates final vote if reached quorum or already confirmed
	 * Requires mutex lock
	 */
	void broadcast_vote_locked (nano::unique_lock<nano::mutex> & lock);
	void remove_votes (nano::block_hash const &);
	void remove_block (nano::block_hash const &);
	bool replace_by_weight (nano::unique_lock<nano::mutex> & lock_a, nano::block_hash const &);
	std::chrono::milliseconds time_to_live () const;
	/**
	 * Calculates minimum time delay between subsequent votes when processing non-final votes
	 */
	std::chrono::seconds cooldown_time (nano::uint128_t weight) const;
	/**
	 * Calculates time delay between broadcasting confirmation requests
	 */
	std::chrono::milliseconds confirm_req_time () const;

private:
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> last_blocks;
	std::unordered_map<nano::account, nano::vote_info> last_votes;
	std::atomic<bool> is_quorum{ false };
	mutable nano::uint128_t final_weight{ 0 };
	mutable std::unordered_map<nano::block_hash, nano::uint128_t> last_tally;

	nano::election_behavior const behavior_m{ nano::election_behavior::normal };
	std::chrono::steady_clock::time_point const election_start = { std::chrono::steady_clock::now () };

	mutable nano::mutex mutex;

public: // Logging
	void operator() (nano::object_stream &) const;

private: // Constants
	static std::size_t constexpr max_blocks{ 10 };

	friend class active_transactions;
	friend class confirmation_solicitor;

public: // Only used in tests
	void force_confirm ();
	std::unordered_map<nano::account, nano::vote_info> votes () const;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks () const;

	friend class confirmation_solicitor_different_hash_Test;
	friend class confirmation_solicitor_bypass_max_requests_cap_Test;
	friend class votes_add_existing_Test;
	friend class votes_add_old_Test;
};
}
