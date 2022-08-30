#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/election.hpp>
#include <nano/node/inactive_cache_information.hpp>
#include <nano/node/inactive_cache_status.hpp>
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
class active_transactions;
class block;
class block_sideband;
class election;
class election_scheduler;
class vote;
class transaction;
class confirmation_height_processor;
class stat;

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

	friend std::unique_ptr<container_info_component> collect_container_info (active_transactions &, std::string const &);
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

	friend std::unique_ptr<container_info_component> collect_container_info (active_transactions &, std::string const &);
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
	// clang-format on

public:
	// clang-format off
	using ordered_roots = boost::multi_index_container<conflict_info,
	mi::indexed_by<
		mi::random_access<mi::tag<tag_random_access>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<conflict_info, nano::qualified_root, &conflict_info::root>>
	>>;
	// clang-format on
	ordered_roots roots;
	using roots_iterator = active_transactions::ordered_roots::index_iterator<tag_root>::type;

	explicit active_transactions (nano::node &, nano::confirmation_height_processor &);
	~active_transactions ();

	/*
	 * Starts new election with hinted behavior type
	 * Hinted elections have shorter timespan and only can take up limited space inside active elections container
	 */
	nano::election_insertion_result insert_hinted (std::shared_ptr<nano::block> const & block_a);
	// Distinguishes replay votes, cannot be determined if the block is not in any election
	nano::vote_code vote (std::shared_ptr<nano::vote> const &);
	// Is the root of this block in the roots container
	bool active (nano::block const &);
	bool active (nano::qualified_root const &);
	/*
	 * Is the block hash present in any active election
	 */
	bool active (nano::block_hash const &);
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

	/*
	 * Maximum number of all elections that should be present in this container.
	 * This is only a soft limit, it is possible for this container to exceed this count.
	 */
	int64_t limit () const;
	/*
	 * Maximum number of hinted elections that should be present in this container.
	 */
	int64_t hinted_limit () const;
	int64_t vacancy () const;
	/*
	 * How many election slots are available for hinted elections.
	 * The limit of AEC taken up by hinted elections is controlled by `node_config::active_elections_hinted_limit_percentage`
	 */
	int64_t vacancy_hinted () const;
	std::function<void ()> vacancy_update{ [] () {} };

	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> blocks;

	nano::election_scheduler & scheduler;
	nano::confirmation_height_processor & confirmation_height_processor;
	nano::node & node;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::active) };
	std::size_t election_winner_details_size ();
	void add_election_winner_details (nano::block_hash const &, std::shared_ptr<nano::election> const &);
	void remove_election_winner_details (nano::block_hash const &);

	nano::vote_generator generator;
	nano::vote_generator final_generator;

	recently_confirmed_cache recently_confirmed;
	recently_cemented_cache recently_cemented;

private:
	nano::mutex election_winner_details_mutex{ mutex_identifier (mutexes::election_winner_details) };

	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> election_winner_details;

	// Call action with confirmed block, may be different than what we started with
	nano::election_insertion_result insert_impl (nano::unique_lock<nano::mutex> &, std::shared_ptr<nano::block> const &, nano::election_behavior = nano::election_behavior::normal, std::function<void (std::shared_ptr<nano::block> const &)> const & = nullptr);
	void request_loop ();
	void request_confirm (nano::unique_lock<nano::mutex> &);
	void erase (nano::qualified_root const &);
	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void cleanup_election (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::election>);
	// Returns a list of elections sorted by difficulty, mutex must be locked
	std::vector<std::shared_ptr<nano::election>> list_active_impl (std::size_t) const;

	/*
	 * Checks if vote passes minimum representative weight threshold and adds it to inactive vote cache
	 * TODO: Should be moved to `vote_cache` class
	 */
	void add_inactive_vote_cache (nano::block_hash const & hash, std::shared_ptr<nano::vote> const vote);

	nano::condition_variable condition;
	bool started{ false };
	std::atomic<bool> stopped{ false };

	// Maximum time an election can be kept active if it is extending the container
	std::chrono::seconds const election_time_to_live;

	int active_hinted_elections_count{ 0 };

	boost::thread thread;

	friend class election;
	friend class election_scheduler;
	friend std::unique_ptr<container_info_component> collect_container_info (active_transactions &, std::string const &);

public: // Tests
	void clear ();

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
