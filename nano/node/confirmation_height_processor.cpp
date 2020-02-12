#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/confirmation_height_processor.hpp>
#include <nano/node/write_database_queue.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/thread/latch.hpp>

#include <cassert>
#include <numeric>

nano::confirmation_height_processor::confirmation_height_processor (nano::ledger & ledger_a, nano::write_database_queue & write_database_queue_a, std::chrono::milliseconds batch_separate_pending_min_time_a, nano::logger_mt & logger_a, boost::latch & latch, confirmation_height_mode mode_a) :
ledger (ledger_a),
write_database_queue (write_database_queue_a),
// clang-format off
confirmation_height_unbounded_processor (ledger_a, write_database_queue_a, batch_separate_pending_min_time_a, logger_a, stopped, original_hash, [this](auto & cemented_blocks) { this->notify_observers (cemented_blocks); }, [this]() { return this->awaiting_processing_size (); }),
confirmation_height_bounded_processor (ledger_a, write_database_queue_a, batch_separate_pending_min_time_a, logger_a, stopped, original_hash, [this](auto & cemented_blocks) { this->notify_observers (cemented_blocks); }, [this]() { return this->awaiting_processing_size (); }),
// clang-format on
thread ([this, &latch, mode_a]() {
	nano::thread_role::set (nano::thread_role::name::confirmation_height_processing);
	// Do not start running the processing thread until other threads have finished their operations
	latch.wait ();
	this->run (mode_a);
})
{
}

nano::confirmation_height_processor::~confirmation_height_processor ()
{
	stop ();
}

void nano::confirmation_height_processor::stop ()
{
	stopped = true;
	condition.notify_one ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void nano::confirmation_height_processor::run (confirmation_height_mode mode_a)
{
	nano::unique_lock<std::mutex> lk (mutex);
	while (!stopped)
	{
		if (!paused && !awaiting_processing.empty ())
		{
			lk.unlock ();
			if (confirmation_height_bounded_processor.pending_empty ())
			{
				confirmation_height_bounded_processor.prepare_new ();
			}
			if (confirmation_height_unbounded_processor.pending_empty ())
			{
				confirmation_height_unbounded_processor.prepare_new ();
			}

			if (confirmation_height_bounded_processor.pending_empty () && confirmation_height_unbounded_processor.pending_empty ())
			{
				lk.lock ();
				original_hashes_pending.clear ();
				lk.unlock ();
			}

			set_next_hash ();

			const auto num_blocks_to_use_unbounded = confirmation_height::batch_write_size;
			auto blocks_within_automatic_unbounded_selection = (ledger.cache.block_count < num_blocks_to_use_unbounded || ledger.cache.block_count - num_blocks_to_use_unbounded < ledger.cache.cemented_count);

			if (mode_a == confirmation_height_mode::unbounded || (mode_a == confirmation_height_mode::automatic && blocks_within_automatic_unbounded_selection))
			{
				confirmation_height_unbounded_processor.process ();
			}
			else
			{
				assert (mode_a == confirmation_height_mode::bounded || mode_a == confirmation_height_mode::automatic);
				confirmation_height_bounded_processor.process ();
			}

			lk.lock ();
		}
		else
		{
			auto lock_and_cleanup = [&lk, this]() {
				for (auto const & observer : cemented_process_finished_observers)
				{
					observer ();
				}
				lk.lock ();
				original_hash.clear ();
				original_hashes_pending.clear ();
			};

			lk.unlock ();
			// If there are blocks pending cementing, then make sure we flush out the remaining writes
			if (!confirmation_height_bounded_processor.pending_empty ())
			{
				assert (confirmation_height_unbounded_processor.pending_empty ());
				auto scoped_write_guard = write_database_queue.wait (nano::writer::confirmation_height);
				confirmation_height_bounded_processor.cement_blocks ();
				lock_and_cleanup ();
			}
			else if (!confirmation_height_unbounded_processor.pending_empty ())
			{
				assert (confirmation_height_bounded_processor.pending_empty ());
				auto scoped_write_guard = write_database_queue.wait (nano::writer::confirmation_height);
				confirmation_height_unbounded_processor.cement_blocks ();
				lock_and_cleanup ();
			}
			else
			{
				lock_and_cleanup ();
				condition.wait (lk);
			}
		}
	}
}

// Pausing only affects processing new blocks, not the current one being processed. Currently only used in tests
void nano::confirmation_height_processor::pause ()
{
	paused = true;
}

void nano::confirmation_height_processor::unpause ()
{
	paused = false;
	condition.notify_one ();
}

void nano::confirmation_height_processor::add (nano::block_hash const & hash_a)
{
	{
		nano::lock_guard<std::mutex> lk (mutex);
		awaiting_processing.insert (hash_a);
	}
	condition.notify_one ();
}

void nano::confirmation_height_processor::set_next_hash ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	assert (!awaiting_processing.empty ());
	original_hash = *awaiting_processing.begin ();
	original_hashes_pending.insert (original_hash);
	awaiting_processing.erase (original_hash);
}

// Not thread-safe, only call before this processor has begun cementing
void nano::confirmation_height_processor::add_cemented_observer (std::function<void(block_w_sideband)> const & callback_a)
{
	cemented_observers.push_back (callback_a);
}

// Not thread-safe, only call before this processor has begun cementing
void nano::confirmation_height_processor::add_cemented_process_finished_observer (std::function<void()> const & callback_a)
{
	cemented_process_finished_observers.push_back (callback_a);
}

void nano::confirmation_height_processor::notify_observers (std::vector<nano::block_w_sideband> const & cemented_blocks)
{
	for (auto const & block_callback_data : cemented_blocks)
	{
		for (auto const & observer : cemented_observers)
		{
			observer (block_callback_data);
		}
	}
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (confirmation_height_processor & confirmation_height_processor_a, const std::string & name_a)
{
	auto composite = std::make_unique<container_info_composite> (name_a);

	size_t cemented_observers_count = confirmation_height_processor_a.cemented_observers.size ();
	size_t cemented_process_finished_observer_count = confirmation_height_processor_a.cemented_process_finished_observers.size ();
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cemented_observers", cemented_observers_count, sizeof (decltype (confirmation_height_processor_a.cemented_observers)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cemented_process_finished_observers", cemented_process_finished_observer_count, sizeof (decltype (confirmation_height_processor_a.cemented_process_finished_observers)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "awaiting_processing", confirmation_height_processor_a.awaiting_processing_size (), sizeof (decltype (confirmation_height_processor_a.awaiting_processing)::value_type) }));
	composite->add_component (collect_container_info (confirmation_height_processor_a.confirmation_height_bounded_processor, "bounded_processor"));
	composite->add_component (collect_container_info (confirmation_height_processor_a.confirmation_height_unbounded_processor, "unbounded_processor"));
	return composite;
}

size_t nano::confirmation_height_processor::awaiting_processing_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return awaiting_processing.size ();
}

bool nano::confirmation_height_processor::is_processing_block (nano::block_hash const & hash_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	return original_hashes_pending.find (hash_a) != original_hashes_pending.cend () || awaiting_processing.find (hash_a) != awaiting_processing.cend ();
}

nano::block_hash nano::confirmation_height_processor::current ()
{
	nano::lock_guard<std::mutex> lk (mutex);
	return original_hash;
}
