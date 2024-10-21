#pragma once

#include <nano/lib/relaxed_atomic.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>

#include <atomic>
#include <chrono>
#include <latch>
#include <memory>
#include <type_traits>

namespace nano
{
class thread_pool final
{
public:
	explicit thread_pool (unsigned num_threads, nano::thread_role::name thread_name) :
		num_threads{ num_threads },
		thread_pool_impl{ std::make_unique<boost::asio::thread_pool> (num_threads) },
		thread_names_latch{ num_threads }
	{
		set_thread_names (thread_name);
	}

	~thread_pool ()
	{
		if (alive ())
		{
			stop ();
		}
	}

	template <typename F>
	void push_task (F && task)
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		if (!stopped)
		{
			++num_tasks;
			release_assert (thread_pool_impl);
			boost::asio::post (*thread_pool_impl, [this, t = std::forward<F> (task)] () mutable {
				t ();
				--num_tasks;
			});
		}
	}

	template <typename F>
	void add_timed_task (std::chrono::steady_clock::time_point const & expiry_time, F && task)
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		if (!stopped)
		{
			release_assert (thread_pool_impl);
			auto timer = std::make_shared<boost::asio::steady_timer> (thread_pool_impl->get_executor ());
			timer->expires_at (expiry_time);
			timer->async_wait ([this, t = std::forward<F> (task), /* preserve lifetime */ timer] (boost::system::error_code const & ec) mutable {
				if (!ec)
				{
					push_task (std::move (t));
				}
			});
		}
	}

	void stop ()
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		if (!stopped)
		{
			stopped = true;
#if defined(BOOST_ASIO_HAS_IOCP)
			// A hack needed for Windows to prevent deadlock during destruction, described here: https://github.com/chriskohlhoff/asio/issues/431
			boost::asio::use_service<boost::asio::detail::win_iocp_io_context> (*thread_pool_m).stop ();
#endif
			lock.unlock ();
			thread_pool_impl->stop ();
			thread_pool_impl->join ();
			lock.lock ();
			thread_pool_impl = nullptr;
		}
	}

	bool alive () const
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		return thread_pool_impl != nullptr;
	}

	unsigned get_num_threads () const
	{
		return num_threads;
	}

	uint64_t num_queued_tasks () const
	{
		return num_tasks;
	}

	nano::container_info container_info () const
	{
		nano::container_info info;
		info.put ("tasks", num_queued_tasks ());
		return info;
	}

private:
	void set_thread_names (nano::thread_role::name thread_name)
	{
		for (auto i = 0u; i < num_threads; ++i)
		{
			boost::asio::post (*thread_pool_impl, [this, thread_name] () {
				nano::thread_role::set (thread_name);
				thread_names_latch.arrive_and_wait ();
			});
		}
		thread_names_latch.wait ();
	}

private:
	unsigned const num_threads;
	mutable nano::mutex mutex;
	std::atomic<bool> stopped{ false };
	std::unique_ptr<boost::asio::thread_pool> thread_pool_impl;
	std::atomic<uint64_t> num_tasks{ 0 };
	std::latch thread_names_latch;
};
}