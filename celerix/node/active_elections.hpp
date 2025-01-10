#pragma once

#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/observer_set.hpp>
#include <celerix/node/election_behavior.hpp>
#include <celerix/node/election_insertion_result.hpp>
#include <celerix/node/election_status.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/recently_cemented_cache.hpp>
#include <celerix/node/recently_confirmed_cache.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/node/vote_with_weight_info.hpp>
#include <celerix/secure/common.hpp>

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

namespace celerix
{
class active_elections_config final
{
public:
	explicit active_elections_config (celerix::network_constants const &);

	celerix::error deserialize (celerix::tomlconfig & toml);
	celerix::error serialize (celerix::tomlconfig & toml) const;

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
	using erased_callback_t = std::function<void (std::shared_ptr<celerix::election>)>;

private: // Elections
	class entry final
	{
	public:
		celerix::qualified_root root;
		std::shared_ptr<celerix::election> election;
		erased_callback_t erased_callback;
	};

	friend class celerix::election;

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
			mi::member<entry, celerix::qualified_root, &entry::root>>
	>>;
	// clang-format on
	ordered_roots roots;

public:
	active_elections (celerix::node &, celerix::confirming_set &, celerix::block_processor &);
	~active_elections ();

	void start ();
	void stop ();

	/**
	 * Starts new election with a specified behavior type
	 */
	celerix::election_insertion_result insert (std::shared_ptr<celerix::block> const &, celerix::election_behavior = celerix::election_behavior::priority, erased_callback_t = nullptr);
	// Is the root of this block in the roots container
	bool active (celerix::block const &) const;
	bool active (celerix::qualified_root const &) const;
	std::shared_ptr<celerix::election> election (celerix::qualified_root const &) const;
	// Returns a list of elections sorted by difficulty
	std::vector<std::shared_ptr<celerix::election>> list_active (std::size_t max_count = std::numeric_limits<std::size_t>::max ());
	bool erase (celerix::block const &);
	bool erase (celerix::qualified_root const &);
	bool empty () const;
	std::size_t size () const;
	std::size_t size (celerix::election_behavior) const;
	bool publish (std::shared_ptr<celerix::block> const &);

	/**
	 * Maximum number of elections that should be present in this container
	 * NOTE: This is only a soft limit, it is possible for this container to exceed this count
	 */
	int64_t limit (celerix::election_behavior behavior) const;
	/**
	 * How many election slots are available for specified election type
	 */
	int64_t vacancy (celerix::election_behavior behavior) const;

	celerix::container_info container_info () const;

public: // Events
	celerix::observer_set<> vacancy_updated;

private:
	void request_loop ();
	void request_confirm (celerix::unique_lock<celerix::mutex> &);
	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void cleanup_election (celerix::unique_lock<celerix::mutex> & lock_a, std::shared_ptr<celerix::election>);

	using block_cemented_result = std::pair<celerix::election_status, std::vector<celerix::vote_with_weight_info>>;
	block_cemented_result block_cemented (std::shared_ptr<celerix::block> const & block, celerix::block_hash const & confirmation_root, std::shared_ptr<celerix::election> const & source_election);
	void notify_observers (celerix::secure::transaction const &, celerix::election_status const & status, std::vector<celerix::vote_with_weight_info> const & votes) const;

	std::shared_ptr<celerix::election> election_impl (celerix::qualified_root const &) const;
	std::vector<std::shared_ptr<celerix::election>> list_active_impl (std::size_t max_count) const;

private: // Dependencies
	active_elections_config const & config;
	celerix::node & node;
	celerix::confirming_set & confirming_set;
	celerix::block_processor & block_processor;

public:
	celerix::recently_confirmed_cache recently_confirmed;
	celerix::recently_cemented_cache recently_cemented;

	// TODO: This mutex is currently public because many tests access it
	// TODO: This is bad. Remove the need to explicitly lock this from any code outside of this class
	mutable celerix::mutex mutex{ mutex_identifier (mutexes::active) };

private:
	/** Keeps track of number of elections by election behavior (normal, hinted, optimistic) */
	celerix::enum_array<celerix::election_behavior, int64_t> count_by_behavior{};

	celerix::condition_variable condition;
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

celerix::stat::type to_stat_type (celerix::election_state);
celerix::stat::detail to_stat_detail (celerix::election_state);
}
