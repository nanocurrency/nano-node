#pragma once

#include <celerix/lib/container_info.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/lib/utility.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace celerix
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
	processing_queue (celerix::stats & stats, celerix::stat::type stat_type, celerix::thread_role::name thread_role, std::size_t thread_count, std::size_t max_queue_size, std::size_t max_batch_size = 0) :
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
				celerix::thread_role::set (thread_role);
				run ();
			});
		}
	}

	void stop ()
	{
		{
			celerix::lock_guard<celerix::mutex> guard{ mutex };
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
		celerix::unique_lock<celerix::mutex> lock{ mutex };
		if (queue.size () < max_queue_size)
		{
			queue.push_back (std::forward<T> (item));
			lock.unlock ();
			condition.notify_one ();
			stats.inc (stat_type, celerix::stat::detail::queue);
		}
		else
		{
			stats.inc (stat_type, celerix::stat::detail::overfill);
		}
	}

	std::size_t size () const
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		return queue.size ();
	}

	celerix::container_info container_info () const
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };

		celerix::container_info info;
		info.put ("queue", queue);
		return info;
	}

private:
	std::deque<value_t> next_batch (celerix::unique_lock<celerix::mutex> & lock)
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
		celerix::unique_lock<celerix::mutex> lock{ mutex };
		while (!stopped)
		{
			auto batch = next_batch (lock);
			if (!batch.empty ())
			{
				lock.unlock ();
				stats.inc (stat_type, celerix::stat::detail::batch);
				process_batch (batch);
				lock.lock ();
			}
		}
	}

public:
	std::function<void (batch_t &)> process_batch{ [] (auto &) { debug_assert (false, "processing queue callback empty"); } };

private:
	celerix::stats & stats;

	const celerix::stat::type stat_type;
	const celerix::thread_role::name thread_role;
	const std::size_t thread_count;
	const std::size_t max_queue_size;
	const std::size_t max_batch_size;

private:
	std::deque<value_t> queue;

	bool stopped{ false };
	mutable celerix::mutex mutex;
	celerix::condition_variable condition;
	std::vector<std::thread> threads;
};
}
