#pragma once

#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <latch>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace boost::asio
{
class thread_pool;
}

namespace nano
{
/*
 * High-performance thread pool implementation that avoids condition variable overhead under high load
 */
class thread_pool final
{
public:
	static constexpr size_t batch_size = 16;
	static constexpr size_t spin_count = 64;
	static constexpr std::chrono::milliseconds wakeup_interval{ 100 };

public:
	thread_pool (unsigned num_threads, nano::thread_role::name thread_name, bool auto_start = false);
	~thread_pool ();

	void start ();
	void stop ();

	void post (std::function<void ()> task);

	bool alive () const;
	uint64_t queued_tasks () const;
	nano::container_info container_info () const;

private:
	void run_worker (unsigned thread_index);

private:
	unsigned const num_threads;
	nano::thread_role::name const thread_name;

	std::deque<std::function<void ()>> tasks;

	std::vector<std::thread> threads;
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::atomic<bool> stopped{ false };

	std::atomic<uint64_t> num_tasks{ 0 };
	std::atomic<uint32_t> sleeping_workers{ 0 };
};

/*
 * Thread pool implementation using Boost.Asio that supports delayed tasks
 */
class timed_thread_pool final
{
public:
	timed_thread_pool (unsigned num_threads, nano::thread_role::name thread_name, bool auto_start = false);
	~timed_thread_pool ();

	void start ();
	void stop ();

	void post (std::function<void ()> task);
	void post_delayed (std::chrono::steady_clock::duration const & delay, std::function<void ()> task);

	bool alive () const;
	uint64_t queued_tasks () const;
	uint64_t delayed_tasks () const;
	nano::container_info container_info () const;

private:
	void set_thread_names ();

private:
	unsigned const num_threads;
	nano::thread_role::name const thread_name;

	std::unique_ptr<boost::asio::thread_pool> thread_pool_impl;

	std::latch thread_names_latch;
	mutable nano::mutex mutex;
	std::atomic<bool> stopped{ false };

	std::atomic<uint64_t> num_tasks{ 0 };
	std::atomic<uint64_t> num_delayed{ 0 };
};
}