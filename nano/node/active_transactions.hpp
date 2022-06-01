#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/election.hpp>
#include <nano/node/voting.hpp>
#include <nano/secure/common.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace nano
{
class node;
class block;
class block_sideband;
class election;
class election_scheduler;
class vote;
class transaction;
class confirmation_height_processor;
class stat;

class cementable_account final
{
public:
	cementable_account (nano::account const & account_a, std::size_t blocks_uncemented_a);
	nano::account account;
	uint64_t blocks_uncemented{ 0 };
};

class election_timepoint final
{
public:
	std::chrono::steady_clock::time_point time;
	nano::qualified_root root;
};

class inactive_cache_status final
{
public:
	bool bootstrap_started{ false };
	bool election_started{ false }; // Did item reach config threshold to start an impromptu election?
	bool confirmed{ false }; // Did item reach votes quorum? (minimum config value)
	nano::uint128_t tally{ 0 }; // Last votes tally for block

	bool operator!= (inactive_cache_status const other) const
	{
		return bootstrap_started != other.bootstrap_started || election_started != other.election_started || confirmed != other.confirmed || tally != other.tally;
	}
};

class inactive_cache_information final
{
public:
	inactive_cache_information () = default;
	inactive_cache_information (std::chrono::steady_clock::time_point arrival, nano::block_hash hash, nano::account initial_rep_a, uint64_t initial_timestamp_a, nano::inactive_cache_status status) :
		arrival (arrival),
		hash (hash),
		status (status)
	{
		voters.reserve (8);
		voters.emplace_back (initial_rep_a, initial_timestamp_a);
	}

	std::chrono::steady_clock::time_point arrival;
	nano::block_hash hash;
	nano::inactive_cache_status status;
	std::vector<std::pair<nano::account, uint64_t>> voters;
	bool needs_eval () const
	{
		return !status.bootstrap_started || !status.election_started || !status.confirmed;
	}
};

class expired_optimistic_election_info final
{
public:
	expired_optimistic_election_info (std::chrono::steady_clock::time_point, nano::account);

	std::chrono::steady_clock::time_point expired_time;
	nano::account account;
	bool election_started{ false };
};

class frontiers_confirmation_info
{
public:
	bool can_start_elections () const;

	std::size_t max_elections{ 0 };
	bool aggressive_mode{ false };
};

class election_insertion_result final
{
public:
	std::shared_ptr<nano::election> election;
	bool inserted{ false };
};

// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions final
{
	class conflict_info final
	{
	public:
		nano::qualified_root root;
		std::shared_ptr<nano::election> election;
		nano::epoch epoch;
		nano::election_behavior election_behavior; // Used to prioritize portion of AEC for vote hinting
	};

	friend class nano::election;

	// clang-format off
	class tag_account {};
	class tag_random_access {};
	class tag_root {};
	class tag_sequence {};
	class tag_uncemented {};
	class tag_arrival {};
	class tag_hash {};
	class tag_expired_time {};
	class tag_election_started {};
	class tag_election_behavior {};
	// clang-format on

public:
	// clang-format off
	using ordered_roots = boost::multi_index_container<conflict_info,
	mi::indexed_by<
		mi::random_access<mi::tag<tag_random_access>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<conflict_info, nano::qualified_root, &conflict_info::root>>,
		mi::hashed_non_unique<mi::tag<tag_election_behavior>,
			mi::member<conflict_info, nano::election_behavior, &conflict_info::election_behavior>>
	>>;
	// clang-format on
	ordered_roots roots;
	using roots_iterator = active_transactions::ordered_roots::index_iterator<tag_root>::type;

	explicit active_transactions (nano::node &, nano::confirmation_height_processor &);
	~active_transactions ();
	// Distinguishes replay votes, cannot be determined if the block is not in any election
	nano::vote_code vote (std::shared_ptr<nano::vote> const &);
	// Is the root of this block in the roots container
	bool active (nano::block const &);
	bool active (nano::qualified_root const &);
	std::shared_ptr<nano::election> election (nano::qualified_root const &) const;
	std::shared_ptr<nano::block> winner (nano::block_hash const &) const;
	// Returns a list of elections sorted by difficulty
	std::vector<std::shared_ptr<nano::election>> list_active (std::size_t = std::numeric_limits<std::size_t>::max ());
	void erase (nano::block const &);
	void erase_hash (nano::block_hash const &);
	void erase_oldest ();
	bool empty ();
	std::size_t size ();
	void stop ();
	bool publish (std::shared_ptr<nano::block> const &);
	boost::optional<nano::election_status_type> confirm_block (nano::transaction const &, std::shared_ptr<nano::block> const &);
	void block_cemented_callback (std::shared_ptr<nano::block> const &);
	void block_already_cemented_callback (nano::block_hash const &);

	int64_t vacancy () const;
	std::function<void ()> vacancy_update{ [] () {} };

	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> blocks;
	std::deque<nano::election_status> list_recently_cemented ();
	std::deque<nano::election_status> recently_cemented;

	void add_recently_cemented (nano::election_status const &);
	void add_recently_confirmed (nano::qualified_root const &, nano::block_hash const &);
	void erase_recently_confirmed (nano::block_hash const &);
	void add_inactive_votes_cache (nano::unique_lock<nano::mutex> &, nano::block_hash const &, nano::account const &, uint64_t const);
	// Inserts an election if conditions are met
	void trigger_inactive_votes_cache_election (std::shared_ptr<nano::block> const &);
	nano::inactive_cache_information find_inactive_votes_cache (nano::block_hash const &);
	void erase_inactive_votes_cache (nano::block_hash const &);
	nano::election_scheduler & scheduler;
	nano::confirmation_height_processor & confirmation_height_processor;
	nano::node & node;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::active) };
	std::size_t priority_cementable_frontiers_size ();
	std::size_t priority_wallet_cementable_frontiers_size ();
	std::size_t inactive_votes_cache_size ();
	std::size_t election_winner_details_size ();
	void add_election_winner_details (nano::block_hash const &, std::shared_ptr<nano::election> const &);
	void remove_election_winner_details (nano::block_hash const &);

	nano::vote_generator generator;
	nano::vote_generator final_generator;

#ifdef MEMORY_POOL_DISABLED
	using allocator = std::allocator<nano::inactive_cache_information>;
#else
	using allocator = boost::fast_pool_allocator<nano::inactive_cache_information>;
#endif

	// clang-format off
	using ordered_cache = boost::multi_index_container<nano::inactive_cache_information,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_arrival>,
			mi::member<nano::inactive_cache_information, std::chrono::steady_clock::time_point, &nano::inactive_cache_information::arrival>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<nano::inactive_cache_information, nano::block_hash, &nano::inactive_cache_information::hash>>>, allocator>;
	// clang-format on

private:
	nano::mutex election_winner_details_mutex{ mutex_identifier (mutexes::election_winner_details) };

	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> election_winner_details;

	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	nano::election_insertion_result insert_impl (nano::unique_lock<nano::mutex> &, std::shared_ptr<nano::block> const&, nano::election_behavior = nano::election_behavior::normal, std::function<void(std::shared_ptr<nano::block>const&)> const & = nullptr);
	// clang-format on
	nano::election_insertion_result insert_hinted (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::block> const & block_a);
	void request_loop ();
	void request_confirm (nano::unique_lock<nano::mutex> &);
	void erase (nano::qualified_root const &);
	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void cleanup_election (nano::unique_lock<nano::mutex> & lock_a, nano::election const &);
	// Returns a list of elections sorted by difficulty, mutex must be locked
	std::vector<std::shared_ptr<nano::election>> list_active_impl (std::size_t) const;

	nano::condition_variable condition;
	bool started{ false };
	std::atomic<bool> stopped{ false };

	// Maximum time an election can be kept active if it is extending the container
	std::chrono::seconds const election_time_to_live;

	static std::size_t constexpr recently_confirmed_size{ 65536 };
	using recent_confirmation = std::pair<nano::qualified_root, nano::block_hash>;
	// clang-format off
	boost::multi_index_container<recent_confirmation,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequence>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<recent_confirmation, nano::qualified_root, &recent_confirmation::first>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<recent_confirmation, nano::block_hash, &recent_confirmation::second>>>>
	recently_confirmed;
	using prioritize_num_uncemented = boost::multi_index_container<nano::cementable_account,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<nano::cementable_account, nano::account, &nano::cementable_account::account>>,
		mi::ordered_non_unique<mi::tag<tag_uncemented>,
			mi::member<nano::cementable_account, uint64_t, &nano::cementable_account::blocks_uncemented>,
			std::greater<uint64_t>>>>;

	boost::multi_index_container<nano::expired_optimistic_election_info,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_expired_time>,
			mi::member<expired_optimistic_election_info, std::chrono::steady_clock::time_point, &expired_optimistic_election_info::expired_time>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<expired_optimistic_election_info, nano::account, &expired_optimistic_election_info::account>>,
		mi::ordered_non_unique<mi::tag<tag_election_started>,
			mi::member<expired_optimistic_election_info, bool, &expired_optimistic_election_info::election_started>, std::greater<bool>>>>
	expired_optimistic_election_infos;
	// clang-format on
	std::atomic<uint64_t> expired_optimistic_election_infos_size{ 0 };

	// Frontiers confirmation
	nano::frontiers_confirmation_info get_frontiers_confirmation_info ();
	void confirm_prioritized_frontiers (nano::transaction const &, uint64_t, uint64_t &);
	void confirm_expired_frontiers_pessimistically (nano::transaction const &, uint64_t, uint64_t &);
	void frontiers_confirmation (nano::unique_lock<nano::mutex> &);
	bool insert_election_from_frontiers_confirmation (std::shared_ptr<nano::block> const &, nano::account const &, nano::uint128_t, nano::election_behavior);
	nano::account next_frontier_account{};
	std::chrono::steady_clock::time_point next_frontier_check{ std::chrono::steady_clock::now () };
	constexpr static std::size_t max_active_elections_frontier_insertion{ 1000 };
	prioritize_num_uncemented priority_wallet_cementable_frontiers;
	prioritize_num_uncemented priority_cementable_frontiers;
	std::unordered_set<nano::wallet_id> wallet_ids_already_iterated;
	std::unordered_map<nano::wallet_id, nano::account> next_wallet_id_accounts;
	bool skip_wallets{ false };
	std::atomic<unsigned> optimistic_elections_count{ 0 };
	void prioritize_frontiers_for_confirmation (nano::transaction const &, std::chrono::milliseconds, std::chrono::milliseconds);
	bool prioritize_account_for_confirmation (prioritize_num_uncemented &, std::size_t &, nano::account const &, nano::account_info const &, uint64_t);
	unsigned max_optimistic ();
	void set_next_frontier_check (bool);
	void add_expired_optimistic_election (nano::election const &);
	bool should_do_frontiers_confirmation () const;
	static std::size_t constexpr max_priority_cementable_frontiers{ 100000 };
	static std::size_t constexpr confirmed_frontiers_max_pending_size{ 10000 };
	static std::chrono::minutes constexpr expired_optimistic_election_info_cutoff{ 30 };
	ordered_cache inactive_votes_cache;
	nano::inactive_cache_status inactive_votes_bootstrap_check (nano::unique_lock<nano::mutex> &, std::vector<std::pair<nano::account, uint64_t>> const &, nano::block_hash const &, nano::inactive_cache_status const &);
	nano::inactive_cache_status inactive_votes_bootstrap_check (nano::unique_lock<nano::mutex> &, nano::account const &, nano::block_hash const &, nano::inactive_cache_status const &);
	nano::inactive_cache_status inactive_votes_bootstrap_check_impl (nano::unique_lock<nano::mutex> &, nano::uint128_t const &, std::size_t, nano::block_hash const &, nano::inactive_cache_status const &);
	nano::inactive_cache_information find_inactive_votes_cache_impl (nano::block_hash const &);
	boost::thread thread;

	friend class election;
	friend class election_scheduler;
	friend std::unique_ptr<container_info_component> collect_container_info (active_transactions &, std::string const &);

	friend class active_transactions_vote_replays_Test;
	friend class frontiers_confirmation_prioritize_frontiers_Test;
	friend class frontiers_confirmation_prioritize_frontiers_max_optimistic_elections_Test;
	friend class confirmation_height_prioritize_frontiers_overwrite_Test;
	friend class active_transactions_confirmation_consistency_Test;
	friend class node_deferred_dependent_elections_Test;
	friend class active_transactions_pessimistic_elections_Test;
	friend class frontiers_confirmation_expired_optimistic_elections_removal_Test;
};

bool purge_singleton_inactive_votes_cache_pool_memory ();
std::unique_ptr<container_info_component> collect_container_info (active_transactions & active_transactions, std::string const & name);
}
