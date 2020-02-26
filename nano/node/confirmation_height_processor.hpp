#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/confirmation_height_bounded.hpp>
#include <nano/node/confirmation_height_unbounded.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace boost
{
class latch;
}
namespace nano
{
class ledger;
class logger_mt;
class write_database_queue;

class confirmation_height_processor final
{
public:
	confirmation_height_processor (nano::ledger &, nano::write_database_queue &, std::chrono::milliseconds, nano::logger_mt &, boost::latch & initialized_latch, confirmation_height_mode = confirmation_height_mode::automatic);
	~confirmation_height_processor ();
	void pause ();
	void unpause ();
	void stop ();
	void add (nano::block_hash const & hash_a);
	void run (confirmation_height_mode);
	size_t awaiting_processing_size ();
	bool is_processing_block (nano::block_hash const &);
	nano::block_hash current ();

	void add_cemented_observer (std::function<void(block_w_sideband)> const &);
	void add_cemented_process_finished_observer (std::function<void()> const &);

private:
	std::mutex mutex;
	// Hashes which have been added to the confirmation height processor, but not yet processed
	std::unordered_set<nano::block_hash> awaiting_processing;
	// Hashes which have been added and processed, but have not been cemented
	std::unordered_set<nano::block_hash> original_hashes_pending;

	/** This is the last block popped off the confirmation height pending collection */
	nano::block_hash original_hash{ 0 };

	nano::condition_variable condition;
	std::atomic<bool> stopped{ false };
	std::atomic<bool> paused{ false };
	std::vector<std::function<void(nano::block_w_sideband)>> cemented_observers;
	std::vector<std::function<void()>> cemented_process_finished_observers;

	nano::ledger & ledger;
	nano::write_database_queue & write_database_queue;
	confirmation_height_unbounded confirmation_height_unbounded_processor;
	confirmation_height_bounded confirmation_height_bounded_processor;
	std::thread thread;

	void set_next_hash ();
	void notify_observers (std::vector<nano::block_w_sideband> const & cemented_blocks);

	friend std::unique_ptr<container_info_component> collect_container_info (confirmation_height_processor &, const std::string &);
	friend class confirmation_height_pending_observer_callbacks_Test;
	friend class confirmation_height_dependent_election_Test;
	friend class confirmation_height_dependent_election_after_already_cemented_Test;
};

std::unique_ptr<container_info_component> collect_container_info (confirmation_height_processor &, const std::string &);
}
