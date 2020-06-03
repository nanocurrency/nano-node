#pragma once

#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_set>

namespace nano
{
class channel;
class confirmation_solicitor;
class node;
class vote_generator_session;
class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	nano::block_hash hash;
};
class election_vote_result final
{
public:
	election_vote_result () = default;
	election_vote_result (bool, bool);
	bool replay{ false };
	bool processed{ false };
};
class election final : public std::enable_shared_from_this<nano::election>
{
	// Minimum time between broadcasts of the current winner of an election, as a backup to requesting confirmations
	std::chrono::milliseconds base_latency () const;
	std::function<void(std::shared_ptr<nano::block>)> confirmation_action;

private: // State management
	enum class state_t
	{
		idle,
		passive, // only listening for incoming votes
		active, // actively request confirmations
		broadcasting, // request confirmations and broadcast the winner
		backtracking, // start an election for unconfirmed dependent blocks
		confirmed, // confirmed but still listening for votes
		expired_confirmed,
		expired_unconfirmed
	};
	static int constexpr passive_duration_factor = 5;
	static int constexpr active_request_count_min = 2;
	static int constexpr active_broadcasting_duration_factor = 30;
	static int constexpr confirmed_duration_factor = 5;
	std::atomic<nano::election::state_t> state_m = { state_t::idle };

	// These time points must be protected by this mutex
	std::mutex timepoints_mutex;
	std::chrono::steady_clock::time_point state_start = { std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_block = { std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_req = { std::chrono::steady_clock::time_point () };

	bool valid_change (nano::election::state_t, nano::election::state_t) const;
	bool state_change (nano::election::state_t, nano::election::state_t);
	void broadcast_block (nano::confirmation_solicitor &);
	void send_confirm_req (nano::confirmation_solicitor &);
	void activate_dependencies ();
	// Calculate votes for local representatives
	void generate_votes (nano::block_hash const &);
	void remove_votes (nano::block_hash const &);
	std::atomic<bool> prioritized_m = { false };

public:
	election (nano::node &, std::shared_ptr<nano::block>, std::function<void(std::shared_ptr<nano::block>)> const &, bool);
	nano::election_vote_result vote (nano::account, uint64_t, nano::block_hash);
	nano::tally_t tally ();
	// Check if we have vote quorum
	bool have_quorum (nano::tally_t const &, nano::uint128_t) const;
	void confirm_once (nano::election_status_type = nano::election_status_type::active_confirmed_quorum);
	// Confirm this block if quorum is met
	void confirm_if_quorum ();
	void log_votes (nano::tally_t const &) const;
	bool publish (std::shared_ptr<nano::block> block_a);
	size_t last_votes_size ();
	void update_dependent ();
	void adjust_dependent_difficulty ();
	size_t insert_inactive_votes_cache (nano::block_hash const &);
	bool prioritized () const;
	void prioritize_election (nano::vote_generator_session &);
	// Calculate votes if the current winner matches \p hash_a
	void try_generate_votes (nano::block_hash const & hash_a);
	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void cleanup ();

public: // State transitions
	bool transition_time (nano::confirmation_solicitor &);
	void transition_passive ();
	void transition_active ();

private:
	void transition_passive_impl ();
	void transition_active_impl ();

public:
	bool idle () const;
	bool confirmed () const;
	nano::node & node;
	std::unordered_map<nano::account, nano::vote_info> last_votes;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks;
	std::chrono::steady_clock::time_point election_start = { std::chrono::steady_clock::now () };
	nano::election_status status;
	unsigned confirmation_request_count{ 0 };
	std::unordered_map<nano::block_hash, nano::uint128_t> last_tally;
	std::unordered_set<nano::block_hash> dependent_blocks;
	std::chrono::seconds late_blocks_delay{ 5 };
	uint64_t const height;

	friend class active_transactions;

	friend class election_bisect_dependencies_Test;
	friend class election_dependencies_open_link_Test;
};
}
