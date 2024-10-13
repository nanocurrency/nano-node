#include <nano/lib/thread_pool.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>

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

nano::container_info nano::thread_pool::container_info () const
{
	nano::container_info info;
	info.put ("count", num_queued_tasks ());
	return info;
}
