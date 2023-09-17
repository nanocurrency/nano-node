#include <nano/lib/config.hpp>
#include <nano/lib/threading.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/format.hpp>

#include <future>
#include <iostream>
#include <thread>

/*
 * thread_attributes
 */

boost::thread::attributes nano::thread_attributes::get_default ()
{
	boost::thread::attributes attrs;
	attrs.set_stack_size (8000000); // 8MB
	return attrs;
}

/*
 * thread_pool
 */

nano::thread_pool::thread_pool (unsigned num_threads, nano::thread_role::name thread_name) :
	num_threads (num_threads),
	thread_pool_m (std::make_unique<boost::asio::thread_pool> (num_threads)),
	thread_names_latch{ num_threads }
{
	set_thread_names (thread_name);
}

nano::thread_pool::~thread_pool ()
{
	stop ();
}

void nano::thread_pool::stop ()
{
	nano::unique_lock<nano::mutex> lk (mutex);
	if (!stopped)
	{
		stopped = true;
#if defined(BOOST_ASIO_HAS_IOCP)
		// A hack needed for Windows to prevent deadlock during destruction, described here: https://github.com/chriskohlhoff/asio/issues/431
		boost::asio::use_service<boost::asio::detail::win_iocp_io_context> (*thread_pool_m).stop ();
#endif
		lk.unlock ();
		thread_pool_m->stop ();
		thread_pool_m->join ();
		lk.lock ();
		thread_pool_m = nullptr;
	}
}

void nano::thread_pool::push_task (std::function<void ()> task)
{
	++num_tasks;
	nano::lock_guard<nano::mutex> guard (mutex);
	if (!stopped)
	{
		boost::asio::post (*thread_pool_m, [this, task] () {
			task ();
			--num_tasks;
		});
	}
}

void nano::thread_pool::add_timed_task (std::chrono::steady_clock::time_point const & expiry_time, std::function<void ()> task)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	if (!stopped && thread_pool_m)
	{
		auto timer = std::make_shared<boost::asio::steady_timer> (thread_pool_m->get_executor (), expiry_time);
		timer->async_wait ([this, task, timer] (boost::system::error_code const & ec) {
			if (!ec)
			{
				push_task (task);
			}
		});
	}
}

unsigned nano::thread_pool::get_num_threads () const
{
	return num_threads;
}

uint64_t nano::thread_pool::num_queued_tasks () const
{
	return num_tasks;
}

void nano::thread_pool::set_thread_names (nano::thread_role::name thread_name)
{
	for (auto i = 0u; i < num_threads; ++i)
	{
		boost::asio::post (*thread_pool_m, [this, thread_name] () {
			nano::thread_role::set (thread_name);
			thread_names_latch.arrive_and_wait ();
		});
	}
	thread_names_latch.wait ();
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (thread_pool & thread_pool, std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "count", thread_pool.num_queued_tasks (), sizeof (std::function<void ()>) }));
	return composite;
}

unsigned int nano::hardware_concurrency ()
{
	// Try to read overridden value from environment variable
	static int value = nano::get_env_int_or_default ("NANO_HARDWARE_CONCURRENCY", 0);
	if (value <= 0)
	{
		// Not present or invalid, use default
		return std::thread::hardware_concurrency ();
	}
	return value;
}

bool nano::join_or_pass (std::thread & thread)
{
	if (thread.joinable ())
	{
		thread.join ();
		return true;
	}
	else
	{
		return false;
	}
}
