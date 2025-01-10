#pragma once

#include <celerix/lib/id_dispenser.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/lib/stats_enums.hpp>
#include <celerix/node/election_status.hpp>
#include <celerix/node/vote_with_weight_info.hpp>
#include <celerix/secure/common.hpp>

#include <atomic>
#include <chrono>
#include <memory>

namespace celerix
{
class block;
class channel;
class confirmation_solicitor;
enum class election_behavior;
class inactive_cache_information;
class node;
enum class vote_code;
enum class vote_source;

class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t timestamp;
	celerix::block_hash hash;
};

// map of vote weight per block, ordered greater first
using tally_t = std::map<celerix::uint128_t, std::shared_ptr<celerix::block>, std::greater<celerix::uint128_t>>;

struct election_extended_status final
{
	celerix::election_status status;
	std::unordered_map<celerix::account, celerix::vote_info> votes;
	std::unordered_map<celerix::block_hash, std::shared_ptr<celerix::block>> blocks;
	celerix::tally_t tally;

	void operator() (celerix::object_stream &) const;
};

enum class election_state
{
	passive, // only listening for incoming votes
	active, // actively request confirmations
	confirmed, // confirmed but still listening for votes
	expired_confirmed,
	expired_unconfirmed,
	cancelled,
};

std::string_view to_string (election_state);
celerix::stat::detail to_stat_detail (election_state);

class election final : public std::enable_shared_from_this<election>
{
	celerix::id_t const id{ celerix::next_id () }; // Track individual objects when tracing

private:
	// Minimum time between broadcasts of the current winner of an election, as a backup to requesting confirmations
	std::chrono::milliseconds base_latency () const;
	std::function<void (std::shared_ptr<celerix::block> const &)> confirmation_action;
	std::function<void (celerix::account const &)> live_vote_action;

private: // State management
	static unsigned constexpr passive_duration_factor = 5;
	static unsigned constexpr active_request_count_min = 2;
	celerix::election_state state_m{ election_state::passive };

	std::chrono::steady_clock::duration state_start{ std::chrono::steady_clock::now ().time_since_epoch () };

	// These are modified while not holding the mutex from transition_time only
	std::chrono::steady_clock::time_point last_block{};
	celerix::block_hash last_block_hash{ 0 };
	std::chrono::steady_clock::time_point last_req{};
	/** The last time vote for this election was generated */
	std::chrono::steady_clock::time_point last_vote{};

	bool valid_change (celerix::election_state, celerix::election_state) const;
	bool state_change (celerix::election_state, celerix::election_state);

public: // State transitions
	bool transition_time (celerix::confirmation_solicitor &);
	void transition_active ();
	bool transition_priority ();
	void cancel ();

public: // Status
	bool confirmed () const;
	bool failed () const;
	celerix::election_extended_status current_status () const;
	std::shared_ptr<celerix::block> winner () const;
	std::chrono::milliseconds duration () const;
	std::atomic<unsigned> confirmation_request_count{ 0 };
	std::atomic<unsigned> vote_broadcast_count{ 0 };

	celerix::tally_t tally () const;
	bool have_quorum (celerix::tally_t const &) const;

	// Guarded by mutex
	celerix::election_status status;

public: // Interface
	election (celerix::node &, std::shared_ptr<celerix::block> const & block, std::function<void (std::shared_ptr<celerix::block> const &)> const & confirmation_action, std::function<void (celerix::account const &)> const & vote_action, celerix::election_behavior behavior);

	std::shared_ptr<celerix::block> find (celerix::block_hash const &) const;
	/*
	 * Process vote. Internally uses cooldown to throttle non-final votes
	 * If the election reaches consensus, it will be confirmed
	 */
	celerix::vote_code vote (celerix::account const & representative, uint64_t timestamp, celerix::block_hash const & block_hash, celerix::vote_source);
	bool publish (std::shared_ptr<celerix::block> const & block_a);
	// Confirm this block if quorum is met
	void confirm_if_quorum (celerix::unique_lock<celerix::mutex> &);
	void try_confirm (celerix::block_hash const & hash);

	/**
	 * Broadcasts vote for the current winner of this election
	 * Checks if sufficient amount of time (`vote_generation_interval`) passed since the last vote generation
	 */
	void broadcast_vote ();
	celerix::vote_info get_last_vote (celerix::account const & account);
	void set_last_vote (celerix::account const & account, celerix::vote_info vote_info);
	celerix::election_status get_status () const;
	std::chrono::steady_clock::time_point get_election_start () const
	{
		return election_start;
	}

private: // Dependencies
	celerix::node & node;

public: // Information
	uint64_t const height;
	celerix::root const root;
	celerix::qualified_root const qualified_root;

	std::vector<celerix::vote_with_weight_info> votes_with_weight () const;
	celerix::election_behavior behavior () const;
	celerix::election_state state () const;

	std::unordered_map<celerix::account, celerix::vote_info> votes () const;
	std::unordered_map<celerix::block_hash, std::shared_ptr<celerix::block>> blocks () const;
	bool contains (celerix::block_hash const &) const;

private:
	celerix::tally_t tally_impl () const;
	bool confirmed_locked () const;
	celerix::election_extended_status current_status_locked () const;
	// lock_a does not own the mutex on return
	void confirm_once (celerix::unique_lock<celerix::mutex> & lock_a);
	bool broadcast_block_predicate () const;
	void broadcast_block (celerix::confirmation_solicitor &);
	void send_confirm_req (celerix::confirmation_solicitor &);
	/**
	 * Broadcast vote for current election winner. Generates final vote if reached quorum or already confirmed
	 * Requires mutex lock
	 */
	void broadcast_vote_locked (celerix::unique_lock<celerix::mutex> & lock);
	void remove_votes (celerix::block_hash const &);
	void remove_block (celerix::block_hash const &);
	bool replace_by_weight (celerix::unique_lock<celerix::mutex> & lock_a, celerix::block_hash const &);
	std::chrono::milliseconds time_to_live () const;
	/**
	 * Calculates minimum time delay between subsequent votes when processing non-final votes
	 */
	std::chrono::seconds cooldown_time (celerix::uint128_t weight) const;
	/**
	 * Calculates time delay between broadcasting confirmation requests
	 */
	std::chrono::milliseconds confirm_req_time () const;

private:
	std::unordered_map<celerix::block_hash, std::shared_ptr<celerix::block>> last_blocks;
	std::unordered_map<celerix::account, celerix::vote_info> last_votes;
	std::atomic<bool> is_quorum{ false };
	mutable celerix::uint128_t final_weight{ 0 };
	mutable std::unordered_map<celerix::block_hash, celerix::uint128_t> last_tally;

	celerix::election_behavior behavior_m;
	std::chrono::steady_clock::time_point const election_start{ std::chrono::steady_clock::now () };

	mutable celerix::mutex mutex;

public: // Logging
	void operator() (celerix::object_stream &) const;

private: // Constants
	static std::size_t constexpr max_blocks{ 10 };

	friend class active_elections;
	friend class confirmation_solicitor;

public: // Only used in tests
	void force_confirm ();

	friend class confirmation_solicitor_different_hash_Test;
	friend class confirmation_solicitor_bypass_max_requests_cap_Test;
	friend class votes_add_existing_Test;
	friend class votes_add_old_Test;
};
}
