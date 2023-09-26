#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/confirmation_height_bounded.hpp>
#include <nano/node/confirmation_height_unbounded.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/component.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <thread>
#include <unordered_set>

namespace mi = boost::multi_index;
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
	confirmation_height_processor (nano::ledger &, nano::write_database_queue &, std::chrono::milliseconds, nano::logging const &, nano::logger_mt &, boost::latch & initialized_latch, confirmation_height_mode = confirmation_height_mode::automatic);
	~confirmation_height_processor ();
	void pause ();
	void unpause ();
	void stop ();
	void add (std::shared_ptr<nano::block> const &);
	void run (confirmation_height_mode);
	std::size_t awaiting_processing_size () const;
	bool is_processing_added_block (nano::block_hash const & hash_a) const;
	bool is_processing_block (nano::block_hash const &) const;
	nano::block_hash current () const;

	/*
	 * Called for each newly cemented block
	 * Called from confirmation height processor thread
	 */
	void add_cemented_observer (std::function<void (std::shared_ptr<nano::block> const &)> const &);
	/*
	 * Called when the block was added to the confirmation height processor but is already confirmed
	 * Called from confirmation height processor thread
	 */
	void add_block_already_cemented_observer (std::function<void (nano::block_hash const &)> const &);

private:
	mutable nano::mutex mutex{ mutex_identifier (mutexes::confirmation_height_processor) };

	// Hashes which have been added to the confirmation height processor, but not yet processed
	struct block_wrapper
	{
		explicit block_wrapper (std::shared_ptr<nano::block> const & block_a) :
			block (block_a)
		{
		}

		std::reference_wrapper<nano::block_hash const> hash () const
		{
			return block->hash ();
		}

		std::shared_ptr<nano::block> block;
	};
	// clang-format off
	class tag_sequence {};
	class tag_hash {};
	boost::multi_index_container<block_wrapper,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequence>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<block_wrapper, std::reference_wrapper<nano::block_hash const>, &block_wrapper::hash>>>> awaiting_processing;
	// clang-format on

	// Hashes which have been added and processed, but have not been cemented
	std::unordered_set<nano::block_hash> original_hashes_pending;
	bool paused{ false };

	/** This is the last block popped off the confirmation height pending collection */
	std::shared_ptr<nano::block> original_block;

	nano::condition_variable condition;
	std::atomic<bool> stopped{ false };
	// No mutex needed for the observers as these should be set up during initialization of the node
	std::vector<std::function<void (std::shared_ptr<nano::block> const &)>> cemented_observers;
	std::vector<std::function<void (nano::block_hash const &)>> block_already_cemented_observers;

	nano::ledger & ledger;
	nano::write_database_queue & write_database_queue;
	/** The maximum amount of blocks to write at once. This is dynamically modified by the bounded processor based on previous write performance **/
	uint64_t batch_write_size{ 16384 };

	confirmation_height_unbounded unbounded_processor;
	confirmation_height_bounded bounded_processor;
	std::thread thread;

	void set_next_hash ();
	void notify_cemented (std::vector<std::shared_ptr<nano::block>> const &);
	void notify_already_cemented (nano::block_hash const &);

	friend std::unique_ptr<container_info_component> collect_container_info (confirmation_height_processor &, std::string const &);

private: // Tests
	friend class confirmation_height_pending_observer_callbacks_Test;
	friend class confirmation_height_dependent_election_Test;
	friend class confirmation_height_dependent_election_after_already_cemented_Test;
	friend class confirmation_height_dynamic_algorithm_no_transition_while_pending_Test;
	friend class confirmation_height_many_accounts_many_confirmations_Test;
	friend class confirmation_height_long_chains_Test;
	friend class confirmation_height_many_accounts_single_confirmation_Test;
	friend class request_aggregator_cannot_vote_Test;
	friend class active_transactions_pessimistic_elections_Test;
};

std::unique_ptr<container_info_component> collect_container_info (confirmation_height_processor &, std::string const &);
}
