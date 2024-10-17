#pragma once

#include <nano/lib/enum_util.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/election_insertion_result.hpp>
#include <nano/node/election_status.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/recently_cemented_cache.hpp>
#include <nano/node/recently_confirmed_cache.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/node/vote_with_weight_info.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace nano
{
class active_elections_config final
{
public:
	explicit active_elections_config (nano::network_constants const &);

	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	// Maximum number of simultaneous active elections (AEC size)
	std::size_t size{ 5000 };
	// Limit of hinted elections as percentage of `active_elections_size`
	std::size_t hinted_limit_percentage{ 20 };
	// Limit of optimistic elections as percentage of `active_elections_size`
	std::size_t optimistic_limit_percentage{ 10 };
	// Maximum confirmation history size
	std::size_t confirmation_history_size{ 2048 };
	// Maximum cache size for recently_confirmed
	std::size_t confirmation_cache{ 65536 };
	// Maximum size of election winner details set
	std::size_t max_election_winners{ 1024 * 16 };
};

/**
 * Core class for determining consensus
 * Holds all active blocks i.e. recently added blocks that need confirmation
 */
class active_elections final
{
public:
	using erased_callback_t = std::function<void (std::shared_ptr<nano::election>)>;

private: // Elections
	class entry final
	{
	public:
		nano::qualified_root root;
		std::shared_ptr<nano::election> election;
		erased_callback_t erased_callback;
	};

	friend class nano::election;

	// clang-format off
	class tag_account {};
	class tag_root {};
	class tag_sequenced {};
	class tag_uncemented {};
	class tag_arrival {};
	class tag_hash {};

	using ordered_roots = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry, nano::qualified_root, &entry::root>>
	>>;
	// clang-format on
	ordered_roots roots;

public:
	active_elections (nano::node &, nano::confirming_set &, nano::block_processor &);
	~active_elections ();

	void start ();
	void stop ();

	/**
	 * Starts new election with a specified behavior type
	 */
	nano::election_insertion_result insert (std::shared_ptr<nano::block> const &, nano::election_behavior = nano::election_behavior::priority, erased_callback_t = nullptr);
	// Is the root of this block in the roots container
	bool active (nano::block const &) const;
	bool active (nano::qualified_root const &) const;
	std::shared_ptr<nano::election> election (nano::qualified_root const &) const;
	// Returns a list of elections sorted by difficulty
	std::vector<std::shared_ptr<nano::election>> list_active (std::size_t = std::numeric_limits<std::size_t>::max ());
	bool erase (nano::block const &);
	bool erase (nano::qualified_root const &);
	bool empty () const;
	std::size_t size () const;
	std::size_t size (nano::election_behavior) const;
	bool publish (std::shared_ptr<nano::block> const &);

	/**
	 * Maximum number of elections that should be present in this container
	 * NOTE: This is only a soft limit, it is possible for this container to exceed this count
	 */
	int64_t limit (nano::election_behavior behavior) const;
	/**
	 * How many election slots are available for specified election type
	 */
	int64_t vacancy (nano::election_behavior behavior) const;
	std::function<void ()> vacancy_update{ [] () {} };

	nano::container_info container_info () const;

private:
	void request_loop ();
	void request_confirm (nano::unique_lock<nano::mutex> &);
	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void cleanup_election (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::election>);
	nano::stat::type completion_type (nano::election const & election) const;
	// Returns a list of elections sorted by difficulty, mutex must be locked
	std::vector<std::shared_ptr<nano::election>> list_active_impl (std::size_t) const;
	void activate_successors (nano::secure::transaction const &, std::shared_ptr<nano::block> const & block);
	void notify_observers (nano::secure::transaction const &, nano::election_status const & status, std::vector<nano::vote_with_weight_info> const & votes) const;
	void block_cemented (nano::secure::transaction const &, std::shared_ptr<nano::block> const & block, nano::block_hash const & confirmation_root, std::shared_ptr<nano::election> const & source_election);

private: // Dependencies
	active_elections_config const & config;
	nano::node & node;
	nano::confirming_set & confirming_set;
	nano::block_processor & block_processor;

public:
	nano::recently_confirmed_cache recently_confirmed;
	nano::recently_cemented_cache recently_cemented;

	// TODO: This mutex is currently public because many tests access it
	// TODO: This is bad. Remove the need to explicitly lock this from any code outside of this class
	mutable nano::mutex mutex{ mutex_identifier (mutexes::active) };

private:
	/** Keeps track of number of elections by election behavior (normal, hinted, optimistic) */
	nano::enum_array<nano::election_behavior, int64_t> count_by_behavior{};

	nano::condition_variable condition;
	bool stopped{ false };
	std::thread thread;

	friend class election;

public: // Tests
	void clear ();

	friend class node_fork_storm_Test;
	friend class system_block_sequence_Test;
	friend class node_mass_block_new_Test;
	friend class active_elections_vote_replays_Test;
	friend class frontiers_confirmation_prioritize_frontiers_Test;
	friend class frontiers_confirmation_prioritize_frontiers_max_optimistic_elections_Test;
	friend class confirmation_height_prioritize_frontiers_overwrite_Test;
	friend class active_elections_confirmation_consistency_Test;
	friend class node_deferred_dependent_elections_Test;
	friend class active_elections_pessimistic_elections_Test;
	friend class frontiers_confirmation_expired_optimistic_elections_removal_Test;
};

nano::stat::type to_stat_type (nano::election_state);
nano::stat::detail to_stat_detail (nano::election_state);
}
