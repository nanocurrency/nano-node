#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace nano
{
class cementing_set_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	bool enable{ true };
	size_t batch_size{ 256 };

	/** Maximum number of dependent blocks to be stored in memory during processing */
	size_t max_blocks{ 128 * 1024 };
	size_t max_queued_notifications{ 8 };

	/** Maximum number of failed blocks to wait for requeuing */
	size_t max_deferred{ 16 * 1024 };
	/** Max age of deferred blocks before they are dropped */
	std::chrono::seconds deferred_age_cutoff{ 15min };
};

/**
 * Set of blocks to be durably confirmed
 */
class cementing_set final
{
	friend class confirmation_heightDeathTest_missing_block_Test;
	friend class confirmation_height_pruned_source_Test;

public:
	cementing_set (cementing_set_config const &, nano::ledger &, nano::ledger_notifications &, nano::stats &, nano::logger &);
	~cementing_set ();

	void start ();
	void stop ();

	// Adds a block to the set of blocks to be confirmed
	void add (nano::block_hash const & hash, std::shared_ptr<nano::election> const & election = nullptr);
	// Added blocks will remain in this set until after ledger has them marked as confirmed.
	bool contains (nano::block_hash const & hash) const;
	std::size_t size () const;

	nano::container_info container_info () const;

public: // Events
	struct context
	{
		std::shared_ptr<nano::block> block;
		nano::block_hash confirmation_root;
		std::shared_ptr<nano::election> election;
	};

	nano::observer_set<std::deque<context> const &> batch_cemented;
	nano::observer_set<std::deque<nano::block_hash> const &> already_cemented;
	nano::observer_set<nano::block_hash> cementing_failed;

	nano::observer_set<std::shared_ptr<nano::block>> cemented_observers;

private: // Dependencies
	cementing_set_config const & config;
	nano::ledger & ledger;
	nano::ledger_notifications & ledger_notifications;
	nano::stats & stats;
	nano::logger & logger;

private:
	struct entry
	{
		nano::block_hash hash;
		std::shared_ptr<nano::election> election;
		std::chrono::steady_clock::time_point timestamp{ std::chrono::steady_clock::now () };
	};

	void run ();
	void run_batch (std::unique_lock<std::mutex> &);
	std::deque<entry> next_batch (size_t max_count);
	void cleanup (std::unique_lock<std::mutex> &);

private:
	// clang-format off
	class tag_hash {};
	class tag_sequenced {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, nano::block_hash, &entry::hash>>
	>>;
	// clang-format on

	// Blocks that are ready to be cemented
	ordered_entries set;
	// Blocks that could not be cemented immediately (e.g. waiting for rollbacks to complete)
	ordered_entries deferred;
	// Blocks that are being cemented in the current batch
	std::unordered_set<nano::block_hash> current;

	std::atomic<bool> stopped{ false };
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::thread thread;

	nano::thread_pool workers;

	nano::interval log_interval;
};
}
