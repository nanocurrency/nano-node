#pragma once

#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_set>

namespace nano
{
class channel;
class confirmation_solicitor;
class inactive_cache_information;
class node;
class vote_generator_session;
class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t timestamp;
	nano::block_hash hash;
};
class vote_with_weight_info final
{
public:
	nano::account representative;
	std::chrono::steady_clock::time_point time;
	uint64_t timestamp;
	nano::block_hash hash;
	nano::uint128_t weight;
};
class election_vote_result final
{
public:
	election_vote_result () = default;
	election_vote_result (bool, bool);
	bool replay{ false };
	bool processed{ false };
};
enum class election_behavior
{
	normal,
	optimistic,
	hinted
};
struct election_extended_status final
{
	nano::election_status status;
	std::unordered_map<nano::account, nano::vote_info> votes;
	nano::tally_t tally;
};
class election final : public std::enable_shared_from_this<nano::election>
{
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
	static unsigned constexpr confirmed_duration_factor = 5;
	std::atomic<nano::election::state_t> state_m = { state_t::passive };

	static_assert (std::is_trivial<std::chrono::steady_clock::duration> ());
	std::atomic<std::chrono::steady_clock::duration> state_start{ std::chrono::steady_clock::now ().time_since_epoch () };

	// These are modified while not holding the mutex from transition_time only
	std::chrono::steady_clock::time_point last_block = { std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_req = { std::chrono::steady_clock::time_point () };

	bool valid_change (nano::election::state_t, nano::election::state_t) const;
	bool state_change (nano::election::state_t, nano::election::state_t);

public: // State transitions
	bool transition_time (nano::confirmation_solicitor &);
	void transition_active ();

public: // Status
	bool confirmed () const;
	bool failed () const;
	bool optimistic () const;
	nano::election_extended_status current_status () const;
	std::shared_ptr<nano::block> winner () const;
	std::atomic<unsigned> confirmation_request_count{ 0 };

	void log_votes (nano::tally_t const &, std::string const & = "") const;
	nano::tally_t tally () const;
	bool have_quorum (nano::tally_t const &) const;

	// Guarded by mutex
	nano::election_status status;

public: // Interface
	election (nano::node &, std::shared_ptr<nano::block> const &, std::function<void (std::shared_ptr<nano::block> const &)> const &, std::function<void (nano::account const &)> const &, nano::election_behavior);
	std::shared_ptr<nano::block> find (nano::block_hash const &) const;
	nano::election_vote_result vote (nano::account const &, uint64_t, nano::block_hash const &);
	bool publish (std::shared_ptr<nano::block> const & block_a);
	std::size_t insert_inactive_votes_cache (nano::inactive_cache_information const &);
	// Confirm this block if quorum is met
	void confirm_if_quorum (nano::unique_lock<nano::mutex> &);

public: // Information
	uint64_t const height;
	nano::root const root;
	nano::qualified_root const qualified_root;
	std::vector<nano::vote_with_weight_info> votes_with_weight () const;

private:
	nano::tally_t tally_impl () const;
	// lock_a does not own the mutex on return
	void confirm_once (nano::unique_lock<nano::mutex> & lock_a, nano::election_status_type = nano::election_status_type::active_confirmed_quorum);
	void broadcast_block (nano::confirmation_solicitor &);
	void send_confirm_req (nano::confirmation_solicitor &);
	// Calculate votes for local representatives
	void generate_votes () const;
	void remove_votes (nano::block_hash const &);
	void remove_block (nano::block_hash const &);
	bool replace_by_weight (nano::unique_lock<nano::mutex> & lock_a, nano::block_hash const &);

private:
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> last_blocks;
	std::unordered_map<nano::account, nano::vote_info> last_votes;
	std::atomic<bool> is_quorum{ false };
	mutable nano::uint128_t final_weight{ 0 };
	mutable std::unordered_map<nano::block_hash, nano::uint128_t> last_tally;

	nano::election_behavior const behavior{ nano::election_behavior::normal };
	std::chrono::steady_clock::time_point const election_start = { std::chrono::steady_clock::now () };

	nano::node & node;
	mutable nano::mutex mutex;

	static std::chrono::seconds constexpr late_blocks_delay{ 5 };
	static std::size_t constexpr max_blocks{ 10 };

	friend class active_transactions;
	friend class confirmation_solicitor;

public: // Only used in tests
	void force_confirm (nano::election_status_type = nano::election_status_type::active_confirmed_quorum);
	std::unordered_map<nano::account, nano::vote_info> votes () const;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks () const;

	friend class confirmation_solicitor_different_hash_Test;
	friend class confirmation_solicitor_bypass_max_requests_cap_Test;
	friend class votes_add_existing_Test;
	friend class votes_add_old_Test;
};
}
