#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/node/fwd.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace nano
{
class confirming_set_config final
{
public:
	// TODO: Serialization & deserialization

public:
	/** Maximum number of dependent blocks to be stored in memory during processing */
	size_t max_blocks{ 128 * 1024 };
	size_t max_queued_notifications{ 8 };
};

/**
 * Set of blocks to be durably confirmed
 */
class confirming_set final
{
	friend class confirmation_heightDeathTest_missing_block_Test;
	friend class confirmation_height_pruned_source_Test;

public:
	confirming_set (confirming_set_config const &, nano::ledger &, nano::stats &);
	~confirming_set ();

	void start ();
	void stop ();

	// Adds a block to the set of blocks to be confirmed
	void add (nano::block_hash const & hash);
	// Added blocks will remain in this set until after ledger has them marked as confirmed.
	bool exists (nano::block_hash const & hash) const;
	std::size_t size () const;

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

public: // Events
	// Observers will be called once ledger has blocks marked as confirmed
	using cemented_t = std::pair<std::shared_ptr<nano::block>, nano::block_hash>; // <block, confirmation root>

	struct cemented_notification
	{
		std::deque<cemented_t> cemented;
		std::deque<nano::block_hash> already_cemented;
	};

	nano::observer_set<cemented_notification const &> batch_cemented;
	nano::observer_set<std::shared_ptr<nano::block>> cemented_observers;

private: // Dependencies
	confirming_set_config const & config;
	nano::ledger & ledger;
	nano::stats & stats;

private:
	void run ();
	void run_batch (std::unique_lock<std::mutex> &);
	std::deque<nano::block_hash> next_batch (size_t max_count);

private:
	std::unordered_set<nano::block_hash> set;

	nano::thread_pool notification_workers;

	bool stopped{ false };
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::thread thread;
};
}
