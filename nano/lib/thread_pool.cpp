#include <nano/lib/thread_pool.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>

#include <condition_variable>
#include <deque>
#include <latch>
#include <thread>
#include <vector>

namespace nano
{
/*
 * thread_pool
 */

thread_pool::thread_pool (unsigned num_threads, nano::thread_role::name thread_name, bool auto_start) :
	num_threads{ num_threads },
	thread_name{ thread_name }
{
	if (auto_start)
	{
		start ();
	}
}

thread_pool::~thread_pool ()
{
	// Must be stopped before destruction to avoid running tasks when node components are being destroyed
	debug_assert (!alive ());
}

void thread_pool::start ()
{
	debug_assert (!stopped.load ());
	debug_assert (threads.empty ());

	for (unsigned i = 0; i < num_threads; ++i)
	{
		threads.emplace_back ([this, i] () {
			nano::thread_role::set (thread_name);
			run_worker (i);
		});
	}
}

void thread_pool::stop ()
{
	{
		std::unique_lock lock{ mutex };
		stopped.store (true, std::memory_order_release);
	}
	condition.notify_all ();

	for (auto & thread : threads)
	{
		if (thread.joinable ())
		{
			thread.join ();
		}
	}
	threads.clear ();
}

void thread_pool::post (std::function<void ()> task)
{
	bool should_notify = false;
	{
		std::lock_guard lock{ mutex };

		tasks.emplace_back (std::move (task));
		num_tasks.fetch_add (1, std::memory_order_relaxed); // Just a counter, relaxed is fine

		auto queue_size = tasks.size ();
		auto sleeping = sleeping_workers.load (std::memory_order_acquire);

		// Only notify if we have sleeping workers and queue isn't too large
		if (sleeping > 0 && queue_size <= num_threads * 2)
		{
			should_notify = true;
		}
	}
	if (should_notify)
	{
		condition.notify_one ();
	}
}

bool thread_pool::alive () const
{
	return !stopped.load () && !threads.empty ();
}

uint64_t thread_pool::queued_tasks () const
{
	return num_tasks.load (std::memory_order_relaxed);
}

nano::container_info thread_pool::container_info () const
{
	nano::container_info info;
	info.put ("tasks", num_tasks.load ());
	return info;
}

void thread_pool::run_worker (unsigned thread_index)
{
	// Thread-local task buffer for batch processing
	std::vector<std::function<void ()>> local_tasks;
	local_tasks.reserve (batch_size);

	while (!stopped.load (std::memory_order_acquire))
	{
		// Spinning phase before blocking
		bool found_work = false;
		for (int spin = 0; spin < spin_count; ++spin) // Moderate spin count
		{
			{
				std::lock_guard lock{ mutex };
				if (!tasks.empty ())
				{
					size_t tasks_to_take = std::min (tasks.size (), size_t{ batch_size });
					for (size_t i = 0; i < tasks_to_take; ++i)
					{
						local_tasks.push_back (std::move (tasks.front ()));
						tasks.pop_front ();
					}
					found_work = true;
					break;
				}
			}

			if (stopped.load (std::memory_order_acquire))
				return;

			// Yield to avoid starving other threads
			std::this_thread::yield ();
		}

		// Process work found during spinning
		if (found_work)
		{
			for (auto & task : local_tasks)
			{
				task ();
				num_tasks.fetch_sub (1, std::memory_order_relaxed);
			}
			local_tasks.clear ();

			continue; // Go back to spinning
		}

		// No work found, go to sleep
		sleeping_workers.fetch_add (1, std::memory_order_acq_rel);
		{
			std::unique_lock lock{ mutex };

			condition.wait_for (lock, wakeup_interval, [this] {
				return !tasks.empty () || stopped.load (std::memory_order_acquire);
			});

			// Grab work after waking while still holding the lock
			if (!tasks.empty ())
			{
				size_t tasks_to_take = std::min (tasks.size (), size_t{ batch_size });
				for (size_t i = 0; i < tasks_to_take; ++i)
				{
					local_tasks.push_back (std::move (tasks.front ()));
					tasks.pop_front ();
				}
			}
		}
		sleeping_workers.fetch_sub (1, std::memory_order_acq_rel);

		// Process tasks outside of lock
		for (auto & task : local_tasks)
		{
			task ();
			num_tasks.fetch_sub (1, std::memory_order_relaxed);
		}
		local_tasks.clear ();
	}
}

/*
 * timed_thread_pool
 */

timed_thread_pool::timed_thread_pool (unsigned num_threads, nano::thread_role::name thread_name, bool auto_start) :
	num_threads{ num_threads },
	thread_name{ thread_name },
	thread_names_latch{ num_threads }
{
	if (auto_start)
	{
		start ();
	}
}

timed_thread_pool::~timed_thread_pool ()
{
	// Must be stopped before destruction to avoid running tasks when node components are being destroyed
	debug_assert (!thread_pool_impl);
}

void timed_thread_pool::start ()
{
	debug_assert (!stopped);
	debug_assert (!thread_pool_impl);
	thread_pool_impl = std::make_unique<boost::asio::thread_pool> (num_threads);
	set_thread_names ();
}

void timed_thread_pool::stop ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	if (!stopped && thread_pool_impl)
	{
		stopped = true;

		lock.unlock ();

		thread_pool_impl->stop ();
		thread_pool_impl->join ();

		lock.lock ();
		thread_pool_impl = nullptr;
	}
}

void timed_thread_pool::post (std::function<void ()> task)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	if (!stopped)
	{
		++num_tasks;
		release_assert (thread_pool_impl);
		boost::asio::post (*thread_pool_impl, [this, t = std::move (task)] () mutable {
			t ();
			--num_tasks;
		});
	}
}

void timed_thread_pool::post_delayed (std::chrono::steady_clock::duration const & delay, std::function<void ()> task)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	if (!stopped)
	{
		++num_delayed;
		release_assert (thread_pool_impl);
		auto timer = std::make_shared<boost::asio::steady_timer> (thread_pool_impl->get_executor ());
		timer->expires_after (delay);
		timer->async_wait ([this, t = std::move (task), /* preserve lifetime */ timer] (boost::system::error_code const & ec) mutable {
			if (!ec)
			{
				--num_delayed;
				post (std::move (t));
			}
		});
	}
}

bool timed_thread_pool::alive () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return thread_pool_impl != nullptr;
}

uint64_t timed_thread_pool::queued_tasks () const
{
	return num_tasks;
}

uint64_t timed_thread_pool::delayed_tasks () const
{
	return num_delayed;
}

nano::container_info timed_thread_pool::container_info () const
{
	nano::container_info info;
	info.put ("tasks", num_tasks);
	info.put ("delayed", num_delayed);
	return info;
}

void timed_thread_pool::set_thread_names ()
{
	for (auto i = 0u; i < num_threads; ++i)
	{
		boost::asio::post (*thread_pool_impl, [this] () {
			nano::thread_role::set (thread_name);
			thread_names_latch.arrive_and_wait ();
		});
	}
	thread_names_latch.wait ();
}
}
