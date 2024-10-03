#pragma once

#include <nano/lib/relaxed_atomic.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <latch>

namespace boost::asio
{
class thread_pool;
}

namespace nano
{
class thread_pool final
{
public:
	explicit thread_pool (unsigned num_threads, nano::thread_role::name);
	~thread_pool ();

	/** This will run when there is an available thread for execution */
	void push_task (std::function<void ()>);

	/** Run a task at a certain point in time */
	void add_timed_task (std::chrono::steady_clock::time_point const & expiry_time, std::function<void ()> task);

	/** Stops any further pushed tasks from executing */
	void stop ();

	/** Number of threads in the thread pool */
	unsigned get_num_threads () const;

	/** Returns the number of tasks which are awaiting execution by the thread pool **/
	uint64_t num_queued_tasks () const;

	nano::container_info container_info () const;

private:
	nano::mutex mutex;
	std::atomic<bool> stopped{ false };
	unsigned num_threads;
	std::unique_ptr<boost::asio::thread_pool> thread_pool_m;
	nano::relaxed_atomic_integral<uint64_t> num_tasks{ 0 };

	/** Set the names of all the threads in the thread pool for easier identification */
	std::latch thread_names_latch;
	void set_thread_names (nano::thread_role::name thread_name);
};
} // namespace nano
