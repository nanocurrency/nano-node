#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace nano
{
class block;
class ledger;
class write_database_queue;
}

namespace nano
{
/**
 * Set of blocks to be durably confirmed
 */
class confirming_set final
{
	friend class confirmation_heightDeathTest_missing_block_Test;
	friend class confirmation_height_pruned_source_Test;

public:
	confirming_set (nano::ledger & ledger, nano::write_database_queue & write_queue, std::chrono::milliseconds batch_time = std::chrono::milliseconds{ 500 });
	~confirming_set ();
	// Adds a block to the set of blocks to be confirmed
	void add (nano::block_hash const & hash);
	void start ();
	void stop ();
	// Added blocks will remain in this set until after ledger has them marked as confirmed.
	bool exists (nano::block_hash const & hash) const;
	std::size_t size () const;
	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

	// Observers will be called once ledger has blocks marked as confirmed
	nano::observer_set<std::shared_ptr<nano::block>> cemented_observers;
	nano::observer_set<nano::block_hash const &> block_already_cemented_observers;

private:
	void run ();
	nano::ledger & ledger;
	nano::write_database_queue & write_queue;
	std::chrono::milliseconds batch_time;
	std::unordered_set<nano::block_hash> set;
	std::unordered_set<nano::block_hash> processing;
	bool stopped{ false };
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::thread thread;
};
}
