#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/node/active_elections_index.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/election_status.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/recently_cemented_cache.hpp>
#include <nano/node/recently_confirmed_cache.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/node/vote_with_weight_info.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>
#include <unordered_map>

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
	// Maximum cache size for recently_confirmed
	std::size_t confirmation_cache{ 1024 * 64 };
	// Maximum size of election winner details set
	std::size_t max_election_winners{ 1024 * 16 };

	std::chrono::milliseconds checkup_interval{ 1s };
	std::chrono::seconds stale_threshold{ nano::is_dev_run () ? 1s : 60s };
};

/**
 * Core class for determining consensus
 * Holds all active blocks i.e. recently added blocks that need confirmation
 */
class active_elections final
{
public:
	using erased_callback_t = std::function<void (std::shared_ptr<nano::election>)>;

public:
	active_elections (nano::node &, nano::ledger_notifications &, nano::cementing_set &);
	~active_elections ();

	void start ();
	void stop ();

	struct insert_result
	{
		std::shared_ptr<nano::election> election;
		bool inserted;
	};

	/// Starts new election
	insert_result insert (
	std::shared_ptr<nano::block> const &,
	nano::election_behavior = nano::election_behavior::priority,
	nano::bucket_index bucket = 0,
	nano::priority_timestamp priority = 0,
	erased_callback_t = nullptr);

	// Notify this container about a new block (potential fork)
	bool publish (std::shared_ptr<nano::block> const &);

	// Trigger an immediate election update (e.g. after it is confirmed)
	bool trigger (nano::qualified_root const &);

	/// Is the root of this block in the roots container
	bool active (nano::block const &) const;
	bool active (nano::qualified_root const &) const;

	std::shared_ptr<nano::election> election (nano::qualified_root const &) const;

	/// Returns a list of elections sorted by difficulty
	std::vector<std::shared_ptr<nano::election>> list_active (std::size_t max_count = std::numeric_limits<std::size_t>::max ());

	bool erase (nano::block const &);
	bool erase (nano::qualified_root const &);

	bool empty () const;

	size_t size () const;
	size_t size (nano::election_behavior) const;
	size_t size (nano::election_behavior, nano::bucket_index) const;

	/// Maximum number of elections that should be present in this container
	/// NOTE: This is only a soft limit, it is possible for this container to exceed this count
	int64_t limit (nano::election_behavior behavior) const;

	/// How many election slots are available for specified election type
	int64_t vacancy (nano::election_behavior behavior) const;

	nano::container_info container_info () const;

public: // Events
	nano::observer_set<> vacancy_updated;
	nano::observer_set<std::shared_ptr<nano::election>, nano::bucket_index, nano::priority_timestamp> election_started;
	nano::observer_set<std::shared_ptr<nano::election>> election_erased;
	nano::observer_set<std::shared_ptr<nano::election>> election_stale;

private:
	bool predicate () const;
	void run ();
	void run_checkup ();
	void tick_elections (nano::unique_lock<nano::mutex> &);
	void checkup_elections (nano::unique_lock<nano::mutex> &);

	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void erase_election (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::election>);

	struct block_cemented_result
	{
		std::shared_ptr<nano::election> election;
		nano::election_status status;
		std::vector<nano::vote_with_weight_info> votes;
	};

	block_cemented_result block_cemented (std::shared_ptr<nano::block> const & block, nano::block_hash const & confirmation_root, std::shared_ptr<nano::election> const & source_election);
	void notify_observers (nano::secure::transaction const &, nano::election_status const & status, std::vector<nano::vote_with_weight_info> const & votes) const;

	std::shared_ptr<nano::election> election_impl (nano::qualified_root const &) const;
	std::vector<std::shared_ptr<nano::election>> list_active_impl (std::size_t max_count = std::numeric_limits<std::size_t>::max ()) const;

private: // Dependencies
	active_elections_config const & config;
	nano::node & node;
	nano::ledger_notifications & ledger_notifications;
	nano::cementing_set & cementing_set;

public:
	nano::active_elections_index index;

	std::unordered_map<nano::qualified_root, erased_callback_t> erased_callbacks;

	nano::recently_confirmed_cache recently_confirmed;
	nano::recently_cemented_cache recently_cemented;

private:
	mutable nano::mutex mutex{ mutex_identifier (mutexes::active) };
	nano::condition_variable condition;
	bool stopped{ false };
	std::thread thread;
	std::thread checkup_thread;

	nano::thread_pool workers;

	nano::interval stale_interval;
	nano::interval warning_interval;

public: // Tests
	void clear ();
};

nano::stat::type to_stat_type (nano::election_state);
nano::stat::detail to_stat_detail (nano::election_state);
}
