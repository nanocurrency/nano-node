#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace nano
{
/**
 * Queue that processes enqueued elements in (possibly parallel) batches
 */
template <typename T>
class processing_queue final
{
public:
	using value_t = T;
	using batch_t = std::deque<value_t>;

	/**
	 * @param thread_role Spawned processing threads will use this name
	 * @param thread_count Number of processing threads
	 * @param max_queue_size Max number of items enqueued, items beyond this value will be discarded
	 * @param max_batch_size Max number of elements processed in single batch, 0 for unlimited (default)
	 */
	processing_queue (nano::stats & stats, nano::stat::type stat_type, nano::thread_role::name thread_role, std::size_t thread_count, std::size_t max_queue_size, std::size_t max_batch_size = 0) :
		stats{ stats },
		stat_type{ stat_type },
		thread_role{ thread_role },
		thread_count{ thread_count },
		max_queue_size{ max_queue_size },
		max_batch_size{ max_batch_size }
	{
	}

	~processing_queue ()
	{
		// Threads must be stopped before destruction
		debug_assert (threads.empty ());
	}

	void start ()
	{
		for (int n = 0; n < thread_count; ++n)
		{
			threads.emplace_back ([this] () {
				nano::thread_role::set (thread_role);
				run ();
			});
		}
	}

	void stop ()
	{
		{
			nano::lock_guard<nano::mutex> guard{ mutex };
			stopped = true;
		}
		condition.notify_all ();
		for (auto & thread : threads)
		{
			thread.join ();
		}
		threads.clear ();
	}

	bool joinable () const
	{
		return std::any_of (threads.cbegin (), threads.cend (), [] (auto const & thread) {
			return thread.joinable ();
		});
	}

	/**
	 * Queues item for batch processing
	 */
	template <class Item>
	void add (Item && item)
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		if (queue.size () < max_queue_size)
		{
			queue.push_back (std::forward<T> (item));
			lock.unlock ();
			condition.notify_one ();
			stats.inc (stat_type, nano::stat::detail::queue);
		}
		else
		{
			stats.inc (stat_type, nano::stat::detail::overfill);
		}
	}

	std::size_t size () const
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		return queue.size ();
	}

public: // Container info
	std::unique_ptr<container_info_component> collect_container_info (std::string const & name)
	{
		nano::lock_guard<nano::mutex> guard{ mutex };

		auto composite = std::make_unique<container_info_composite> (name);
		composite->add_component (std::make_unique<container_info_leaf> (container_info{ "queue", queue.size (), sizeof (typename decltype (queue)::value_type) }));
		return composite;
	}

private:
	std::deque<value_t> next_batch (nano::unique_lock<nano::mutex> & lock)
	{
		release_assert (lock.owns_lock ());

		condition.wait (lock, [this] () {
			return stopped || !queue.empty ();
		});

		if (stopped)
		{
			return {};
		}

		debug_assert (!queue.empty ());

		// Unlimited batch size or queue smaller than max batch size, return the whole current queue
		if (max_batch_size == 0 || queue.size () < max_batch_size)
		{
			decltype (queue) queue_l;
			queue_l.swap (queue);
			return queue_l;
		}
		// Larger than max batch size, return limited number of elements
		else
		{
			decltype (queue) queue_l;
			for (int n = 0; n < max_batch_size; ++n)
			{
				debug_assert (!queue.empty ());
				queue_l.push_back (std::move (queue.front ()));
				queue.pop_front ();
			}
			return queue_l;
		}
	}

	void run ()
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		while (!stopped)
		{
			auto batch = next_batch (lock);
			if (!batch.empty ())
			{
				lock.unlock ();
				stats.inc (stat_type, nano::stat::detail::batch);
				process_batch (batch);
				lock.lock ();
			}
		}
	}

public:
	std::function<void (batch_t &)> process_batch{ [] (auto &) { debug_assert (false, "processing queue callback empty"); } };

private:
	nano::stats & stats;

	const nano::stat::type stat_type;
	const nano::thread_role::name thread_role;
	const std::size_t thread_count;
	const std::size_t max_queue_size;
	const std::size_t max_batch_size;

private:
	std::deque<value_t> queue;

	bool stopped{ false };
	mutable nano::mutex mutex;
	nano::condition_variable condition;
	std::vector<std::thread> threads;
};
}
