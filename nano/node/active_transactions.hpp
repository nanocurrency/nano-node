#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/election_insertion_result.hpp>
#include <nano/node/election_status.hpp>
#include <nano/node/vote_with_weight_info.hpp>
#include <nano/node/voting.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace nano
{
class node;
class active_transactions;
class block;
class block_sideband;
class block_processor;
class election;
class vote;
class confirmation_height_processor;
class stats;

class recently_confirmed_cache final
{
public:
	using entry_t = std::pair<nano::qualified_root, nano::block_hash>;

	explicit recently_confirmed_cache (std::size_t max_size);

	void put (nano::qualified_root const &, nano::block_hash const &);
	void erase (nano::block_hash const &);
	void clear ();
	std::size_t size () const;

	bool exists (nano::qualified_root const &) const;
	bool exists (nano::block_hash const &) const;

public: // Tests
	entry_t back () const;

private:
	// clang-format off
	class tag_root {};
	class tag_hash {};

	using ordered_recent_confirmations = boost::multi_index_container<entry_t,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequence>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry_t, nano::qualified_root, &entry_t::first>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry_t, nano::block_hash, &entry_t::second>>>>;
	// clang-format on
	ordered_recent_confirmations confirmed;

	std::size_t const max_size;

	mutable nano::mutex mutex;

public: // Container info
	std::unique_ptr<container_info_component> collect_container_info (std::string const &);
};

/*
 * Helper container for storing recently cemented elections (a block from election might be confirmed but not yet cemented by confirmation height processor)
 */
class recently_cemented_cache final
{
public:
	using queue_t = std::deque<nano::election_status>;

	explicit recently_cemented_cache (std::size_t max_size);

	void put (nano::election_status const &);
	queue_t list () const;
	std::size_t size () const;

private:
	queue_t cemented;
	std::size_t const max_size;

	mutable nano::mutex mutex;

public: // Container info
	std::unique_ptr<container_info_component> collect_container_info (std::string const &);
};

/**
 * Core class for determining consensus
 * Holds all active blocks i.e. recently added blocks that need confirmation
 */
class active_transactions final
{
private: // Elections
	class conflict_info final
	{
	public:
		nano::qualified_root root;
		std::shared_ptr<nano::election> election;
	};

	friend class nano::election;

	// clang-format off
	class tag_account {};
	class tag_root {};
	class tag_sequenced {};
	class tag_uncemented {};
	class tag_arrival {};
	class tag_hash {};

	using ordered_roots = boost::multi_index_container<conflict_info,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<conflict_info, nano::qualified_root, &conflict_info::root>>
	>>;
	// clang-format on
	ordered_roots roots;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> blocks;

public:
	active_transactions (nano::node &, nano::confirmation_height_processor &, nano::block_processor &);
	~active_transactions ();

	void start ();
	void stop ();

	/**
	 * Starts new election with a specified behavior type
	 */
	nano::election_insertion_result insert (std::shared_ptr<nano::block> const &, nano::election_behavior = nano::election_behavior::normal);
	// Distinguishes replay votes, cannot be determined if the block is not in any election
	nano::vote_code vote (std::shared_ptr<nano::vote> const &);
	// Is the root of this block in the roots container
	bool active (nano::block const &) const;
	bool active (nano::qualified_root const &) const;
	/**
	 * Is the block hash present in any active election
	 */
	bool active (nano::block_hash const &) const;
	std::shared_ptr<nano::election> election (nano::qualified_root const &) const;
	std::shared_ptr<nano::block> winner (nano::block_hash const &) const;
	// Returns a list of elections sorted by difficulty
	std::vector<std::shared_ptr<nano::election>> list_active (std::size_t = std::numeric_limits<std::size_t>::max ());
	void erase (nano::block const &);
	void erase_hash (nano::block_hash const &);
	void erase_oldest ();
	bool empty () const;
	std::size_t size () const;
	bool publish (std::shared_ptr<nano::block> const &);
	void block_cemented_callback (std::shared_ptr<nano::block> const &);
	void block_already_cemented_callback (nano::block_hash const &);

	/**
	 * Maximum number of elections that should be present in this container
	 * NOTE: This is only a soft limit, it is possible for this container to exceed this count
	 */
	int64_t limit (nano::election_behavior behavior = nano::election_behavior::normal) const;
	/**
	 * How many election slots are available for specified election type
	 */
	int64_t vacancy (nano::election_behavior behavior = nano::election_behavior::normal) const;
	std::function<void ()> vacancy_update{ [] () {} };

	std::size_t election_winner_details_size ();
	void add_election_winner_details (nano::block_hash const &, std::shared_ptr<nano::election> const &);
	std::shared_ptr<nano::election> remove_election_winner_details (nano::block_hash const &);

private:
	// Erase elections if we're over capacity
	void trim ();
	void request_loop ();
	void request_confirm (nano::unique_lock<nano::mutex> &);
	void erase (nano::qualified_root const &);
	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void cleanup_election (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::election>);
	nano::stat::type completion_type (nano::election const & election) const;
	// Returns a list of elections sorted by difficulty, mutex must be locked
	std::vector<std::shared_ptr<nano::election>> list_active_impl (std::size_t) const;
	/**
	 * Checks if vote passes minimum representative weight threshold and adds it to inactive vote cache
	 * TODO: Should be moved to `vote_cache` class
	 */
	void add_vote_cache (nano::block_hash const & hash, std::shared_ptr<nano::vote> vote);
	void activate_successors (nano::store::read_transaction const & transaction, std::shared_ptr<nano::block> const & block);
	void notify_observers (nano::store::read_transaction const & transaction, nano::election_status const & status, std::vector<nano::vote_with_weight_info> const & votes);

private: // Dependencies
	nano::node & node;
	nano::confirmation_height_processor & confirmation_height_processor;
	nano::block_processor & block_processor;

public:
	recently_confirmed_cache recently_confirmed;
	recently_cemented_cache recently_cemented;

	// TODO: This mutex is currently public because many tests access it
	// TODO: This is bad. Remove the need to explicitly lock this from any code outside of this class
	mutable nano::mutex mutex{ mutex_identifier (mutexes::active) };

private:
	nano::mutex election_winner_details_mutex{ mutex_identifier (mutexes::election_winner_details) };
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> election_winner_details;

	// Maximum time an election can be kept active if it is extending the container
	std::chrono::seconds const election_time_to_live;

	/** Keeps track of number of elections by election behavior (normal, hinted, optimistic) */
	nano::enum_array<nano::election_behavior, int64_t> count_by_behavior;

	nano::condition_variable condition;
	bool stopped{ false };
	std::thread thread;

	friend class election;
	friend std::unique_ptr<container_info_component> collect_container_info (active_transactions &, std::string const &);

public: // Tests
	void clear ();

	friend class node_fork_storm_Test;
	friend class system_block_sequence_Test;
	friend class node_mass_block_new_Test;
	friend class active_transactions_vote_replays_Test;
	friend class frontiers_confirmation_prioritize_frontiers_Test;
	friend class frontiers_confirmation_prioritize_frontiers_max_optimistic_elections_Test;
	friend class confirmation_height_prioritize_frontiers_overwrite_Test;
	friend class active_transactions_confirmation_consistency_Test;
	friend class node_deferred_dependent_elections_Test;
	friend class active_transactions_pessimistic_elections_Test;
	friend class frontiers_confirmation_expired_optimistic_elections_removal_Test;
};

std::unique_ptr<container_info_component> collect_container_info (active_transactions & active_transactions, std::string const & name);
}
