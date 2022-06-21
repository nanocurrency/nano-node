#pragma once

#include <nano/boost/asio/deadline_timer.hpp>
#include <nano/boost/asio/executor_work_guard.hpp>
#include <nano/boost/asio/io_context.hpp>
#include <nano/boost/asio/steady_timer.hpp>
#include <nano/boost/asio/thread_pool.hpp>
#include <nano/lib/utility.hpp>

#include <boost/thread/thread.hpp>

#include <thread>

namespace nano
{
/*
 * Functions for understanding the role of the current thread
 */
namespace thread_role
{
	enum class name
	{
		unknown,
		io,
		work,
		packet_processing,
		vote_processing,
		block_processing,
		request_loop,
		wallet_actions,
		bootstrap_initiator,
		bootstrap_connections,
		voting,
		signature_checking,
		rpc_request_processor,
		rpc_process_container,
		confirmation_height_processing,
		worker,
		request_aggregator,
		state_block_signature_verification,
		epoch_upgrader,
		db_parallel_traversal,
		election_scheduler,
		unchecked,
		election_hinting,
	};

	/*
	 * Get/Set the identifier for the current thread
	 */
	nano::thread_role::name get ();
	void set (nano::thread_role::name);

	/*
	 * Get the thread name as a string from enum
	 */
	std::string get_string (nano::thread_role::name);

	/*
	 * Get the current thread's role as a string
	 */
	std::string get_string ();

	/*
	 * Internal only, should not be called directly
	 */
	void set_os_name (std::string const &);
}

namespace thread_attributes
{
	void set (boost::thread::attributes &);
}

class thread_runner final
{
public:
	thread_runner (boost::asio::io_context &, unsigned);
	~thread_runner ();
	/** Tells the IO context to stop processing events.*/
	void stop_event_processing ();
	/** Wait for IO threads to complete */
	void join ();
	std::vector<boost::thread> threads;
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_guard;
};

/* Default memory order of normal std::atomic operations is std::memory_order_seq_cst which provides
   a total global ordering of atomic operations as well as synchronization between threads. Weaker memory
   ordering can provide benefits in some circumstances, like dumb counters where no other data is
   dependent on the ordering of these operations. This assumes T is a type of integer, not bool or char. */
template <typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
class relaxed_atomic_integral
{
public:
	relaxed_atomic_integral () noexcept = default;
	constexpr relaxed_atomic_integral (T desired) noexcept :
		atomic (desired)
	{
	}

	T operator= (T desired) noexcept
	{
		store (desired);
		return atomic;
	}

	relaxed_atomic_integral (relaxed_atomic_integral const &) = delete;
	relaxed_atomic_integral & operator= (relaxed_atomic_integral const &) = delete;

	void store (T desired, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		atomic.store (desired, order);
	}

	T load (std::memory_order order = std::memory_order_relaxed) const noexcept
	{
		return atomic.load (std::memory_order_relaxed);
	}

	operator T () const noexcept
	{
		return load ();
	}

	bool compare_exchange_weak (T & expected, T desired, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		return atomic.compare_exchange_weak (expected, desired, order);
	}

	bool compare_exchange_strong (T & expected, T desired, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		return atomic.compare_exchange_strong (expected, desired, order);
	}

	T fetch_add (T arg, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		return atomic.fetch_add (arg, order);
	}

	T fetch_sub (T arg, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		return atomic.fetch_sub (arg, order);
	}

	T operator++ () noexcept
	{
		return fetch_add (1) + 1;
	}

	T operator++ (int) noexcept
	{
		return fetch_add (1);
	}

	T operator-- () noexcept
	{
		return fetch_sub (1) - 1;
	}

	T operator-- (int) noexcept
	{
		return fetch_sub (1);
	}

private:
	std::atomic<T> atomic;
};

class thread_pool final
{
public:
	explicit thread_pool (unsigned, nano::thread_role::name);
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

private:
	nano::mutex mutex;
	std::atomic<bool> stopped{ false };
	unsigned num_threads;
	std::unique_ptr<boost::asio::thread_pool> thread_pool_m;
	relaxed_atomic_integral<uint64_t> num_tasks{ 0 };

	void set_thread_names (unsigned num_threads, nano::thread_role::name thread_name);
};

std::unique_ptr<nano::container_info_component> collect_container_info (thread_pool & thread_pool, std::string const & name);
}
