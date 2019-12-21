#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/gap_cache.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
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
class vote;
class transaction;

class conflict_info final
{
public:
	nano::qualified_root root;
	uint64_t difficulty;
	uint64_t adjusted_difficulty;
	std::shared_ptr<nano::election> election;
};

enum class election_status_type : uint8_t
{
	ongoing = 0,
	active_confirmed_quorum = 1,
	active_confirmation_height = 2,
	inactive_confirmation_height = 3,
	stopped = 5
};

class election_status final
{
public:
	std::shared_ptr<nano::block> winner;
	nano::amount tally;
	std::chrono::milliseconds election_end;
	std::chrono::milliseconds election_duration;
	unsigned confirmation_request_count;
	unsigned block_count;
	unsigned voter_count;
	election_status_type type;
};

class cementable_account final
{
public:
	cementable_account (nano::account const & account_a, size_t blocks_uncemented_a);
	nano::account account;
	uint64_t blocks_uncemented{ 0 };
};

class election_timepoint final
{
public:
	std::chrono::steady_clock::time_point time;
	nano::qualified_root root;
};

class active_state
{
protected:
	enum class state_impl : uint8_t
	{
		existing_active = 0,
		existing_passive = 1,
		inserted_active = 3,
		inserted_passive = 4,
		not_found = 5,
		unknown = 6
	};
	state_impl state{ state_impl::unknown };
	friend class active_transactions;

public:
	bool exists () const
	{
		return state != state_impl::not_found && state != state_impl::unknown;
	}
	bool active () const
	{
		return state == state_impl::existing_active || state == state_impl::inserted_active;
	}
	bool passive () const
	{
		return state == state_impl::existing_passive || state == state_impl::inserted_passive;
	}
	operator bool () const
	{
		return exists ();
	}
	std::shared_ptr<nano::block> current_election_winner{ nullptr };
};

class active_insertion_state final : public active_state
{
public:
	bool inserted () const
	{
		return state == state_impl::inserted_active || state == state_impl::inserted_passive;
	}
};

// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions final
{
	// clang-format off
	class tag_account {};
	class tag_difficulty {};
	class tag_sequence {};
	class tag_uncemented {};
	// clang-format on

public:
	explicit active_transactions (nano::node &);
	~active_transactions ();
	// Start an election for a block
	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	nano::active_insertion_state start (std::shared_ptr<nano::block>, bool const = false, std::function<void(std::shared_ptr<nano::block>)> const & = [](std::shared_ptr<nano::block>) {});
	// clang-format on
	// If this returns true, the vote is a replay. If this returns false, the vote may or may not be a replay
	bool vote (std::shared_ptr<nano::vote>, bool = false);
	// Find the state of an election by root
	nano::active_state state (nano::qualified_root const &);
	nano::active_state state (nano::block const &);
	// Find the state of an election by hash. A hash may not yet have been associated with an election, prefer searching by root
	nano::active_state state (nano::block_hash const &);
	// Update the difficulty of an election with a new block
	void update_difficulty (std::shared_ptr<nano::block>, boost::optional<nano::write_transaction const &> = boost::none);
	// Adjust the difficulty of a block and its dependants. This helps prioritize head blocks, since the others can be confirmed with confirmation_height
	void adjust_difficulty (nano::block_hash const &);
	// Sample active difficulty from all non-recent active elections
	void update_active_difficulty (nano::unique_lock<std::mutex> &);
	uint64_t active_difficulty ();
	// Limited active difficulty, by the configured max work generation difficulty
	uint64_t limited_active_difficulty ();
	std::deque<std::shared_ptr<nano::block>> list_blocks (bool = false);
	// Stops and clears the artifacts of the election for the root of a block
	bool erase (nano::block const &);
	bool empty ();
	// The number of ongoing elections
	size_t size ();
	// The number of ongoing active elections, for which the node is requesting and republishing votes
	size_t active_size ();
	// The number of ongoing passive elections, for which the node has not voted yet, is ignoring requests and not republishing arriving votes
	size_t passive_size ();
	void stop ();
	// Called when a block is seen via publish or confirm_ack
	bool publish (std::shared_ptr<nano::block> block_a);
	boost::optional<nano::election_status_type> confirm_block (nano::transaction const &, std::shared_ptr<nano::block>);
	void post_confirmation_height_set (nano::transaction const & transaction_a, std::shared_ptr<nano::block> block_a, nano::block_sideband const & sideband_a, nano::election_status_type election_status_type_a);
	// Conainer of elections, indexed by root and ordered in descending order of [adjusted] difficulty. After index node.active_elections_size are the passive elections
	// clang-format off
	class tag_root {};
	boost::multi_index_container<nano::conflict_info,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<nano::conflict_info, nano::qualified_root, &nano::conflict_info::root>>,
		mi::ordered_non_unique<mi::tag<tag_difficulty>,
			mi::member<nano::conflict_info, uint64_t, &nano::conflict_info::adjusted_difficulty>,
			std::greater<uint64_t>>>>
	roots;
	// clang-format on
	boost::optional<uint64_t> last_active_root_difficulty{ boost::none };
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> blocks;
	std::deque<nano::election_status> list_confirmed ();
	std::deque<nano::election_status> confirmed;
	void add_confirmed (nano::election_status const &, nano::qualified_root const &);
	void add_inactive_votes_cache (nano::block_hash const &, nano::account const &);
	nano::gap_information find_inactive_votes_cache (nano::block_hash const &);
	nano::node & node;
	std::mutex mutex;
	std::chrono::seconds const long_election_threshold;
	// Delay until requesting confirmation for an election
	std::chrono::milliseconds const election_request_delay;
	// Maximum time an election can be kept active if it is extending the container
	std::chrono::seconds const election_time_to_live;
	static size_t constexpr max_block_broadcasts = 30;
	static size_t constexpr max_confirm_representatives = 30;
	static size_t constexpr max_confirm_req_batches = 20;
	static size_t constexpr max_confirm_req = 5;
	boost::circular_buffer<double> multipliers_cb;
	uint64_t trended_active_difficulty;
	size_t priority_cementable_frontiers_size ();
	size_t priority_wallet_cementable_frontiers_size ();
	boost::circular_buffer<double> difficulty_trend ();
	size_t inactive_votes_cache_size ();
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> pending_conf_height;
	void clear_block (nano::block_hash const & hash_a);

private:
	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	nano::active_insertion_state add (std::shared_ptr<nano::block>, bool const = false, std::function<void(std::shared_ptr<nano::block>)> const & = [](std::shared_ptr<nano::block>) {});
	// clang-format on
	// Stops and clears the artifacts of the election for root
	bool erase (nano::qualified_root const &);
	void request_loop ();
	// Searches the ledger for unconfirmed frontiers and starts elections. Only used when not saturated
	void search_frontiers (nano::transaction const &);
	// Elections taking too long get escalated after some confirmation requests and time
	void election_escalate (std::shared_ptr<nano::election> &, nano::transaction const &, size_t const &);
	// Queues broadcasting of the current winner of an election with the goal of receiving votes for it or a conflicting block
	void election_broadcast (std::shared_ptr<nano::election> &, nano::transaction const &, std::deque<std::shared_ptr<nano::block>> &, std::unordered_set<nano::qualified_root> &, nano::qualified_root &);
	// Request confirmation from principal representatives whose vote has not yet been seen
	bool election_request_confirm (std::shared_ptr<nano::election> &, std::vector<nano::representative> const &, size_t const &,
	std::deque<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<std::vector<std::shared_ptr<nano::transport::channel>>>>> & single_confirm_req_bundle_l,
	std::unordered_map<std::shared_ptr<nano::transport::channel>, std::deque<std::pair<nano::block_hash, nano::root>>> & batched_confirm_req_bundle_l);
	// Main internal loop, goes through roots requesting confirmations periodically
	void request_confirm (nano::unique_lock<std::mutex> &);
	nano::account next_frontier_account{ 0 };
	std::chrono::steady_clock::time_point next_frontier_check{ std::chrono::steady_clock::now () };
	nano::condition_variable condition;
	bool started{ false };
	std::atomic<bool> stopped{ false };
	size_t max_roots;
	unsigned ongoing_broadcasts{ 0 };
	// clang-format off
	boost::multi_index_container<nano::qualified_root,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequence>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::identity<nano::qualified_root>>>>
	confirmed_set;
	using prioritize_num_uncemented = boost::multi_index_container<nano::cementable_account,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<nano::cementable_account, nano::account, &nano::cementable_account::account>>,
		mi::ordered_non_unique<mi::tag<tag_uncemented>,
			mi::member<nano::cementable_account, uint64_t, &nano::cementable_account::blocks_uncemented>,
			std::greater<uint64_t>>>>;
	// clang-format on
	prioritize_num_uncemented priority_wallet_cementable_frontiers;
	prioritize_num_uncemented priority_cementable_frontiers;
	void prioritize_frontiers_for_confirmation (nano::transaction const &, std::chrono::milliseconds, std::chrono::milliseconds);
	std::unordered_set<nano::wallet_id> wallet_ids_already_iterated;
	std::unordered_map<nano::wallet_id, nano::account> next_wallet_id_accounts;
	bool skip_wallets{ false };
	void prioritize_account_for_confirmation (prioritize_num_uncemented &, size_t &, nano::account const &, nano::account_info const &, uint64_t);
	static size_t constexpr max_priority_cementable_frontiers{ 100000 };
	static size_t constexpr confirmed_frontiers_max_pending_cut_off{ 1000 };
	nano::gap_cache::ordered_gaps inactive_votes_cache;
	static size_t constexpr inactive_votes_cache_max{ 16 * 1024 };
	// clang-format off
	boost::multi_index_container<nano::election_timepoint,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequence>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<nano::election_timepoint, nano::qualified_root, &nano::election_timepoint::root>>>>
	dropped_elections_cache;
	// clang-format on
	static size_t constexpr dropped_elections_cache_max{ 32 * 1024 };
	boost::thread thread;

	friend class confirmation_height_prioritize_frontiers_Test;
	friend class confirmation_height_prioritize_frontiers_overwrite_Test;
	friend class confirmation_height_many_accounts_single_confirmation_Test;
	friend class confirmation_height_many_accounts_many_confirmations_Test;
	friend class confirmation_height_long_chains_Test;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (active_transactions & active_transactions, const std::string & name);
}
