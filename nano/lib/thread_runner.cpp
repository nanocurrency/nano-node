#include <nano/lib/thread_runner.hpp>

#include <iostream>

/*
 * thread_runner
 */

nano::thread_runner::thread_runner (boost::asio::io_context & io_ctx_a, unsigned num_threads, const nano::thread_role::name thread_role_a) :
	io_guard{ boost::asio::make_work_guard (io_ctx_a) },
	role{ thread_role_a }
{
	for (auto i (0u); i < num_threads; ++i)
	{
		threads.emplace_back (nano::thread_attributes::get_default (), [this, &io_ctx_a] () {
			nano::thread_role::set (role);
			try
			{
				run (io_ctx_a);
			}
			catch (std::exception const & ex)
			{
				std::cerr << ex.what () << std::endl;
#ifndef NDEBUG
				throw; // Re-throw to debugger in debug mode
#endif
			}
			catch (...)
			{
#ifndef NDEBUG
				throw; // Re-throw to debugger in debug mode
#endif
			}
		});
	}
}

nano::thread_runner::~thread_runner ()
{
	join ();
}

void nano::thread_runner::run (boost::asio::io_context & io_ctx_a)
{
#if NANO_ASIO_HANDLER_TRACKING == 0
	io_ctx_a.run ();
#else
	nano::timer<> timer;
	timer.start ();
	while (true)
	{
		timer.restart ();
		// Run at most 1 completion handler and record the time it took to complete (non-blocking)
		auto count = io_ctx_a.poll_one ();
		if (count == 1 && timer.since_start ().count () >= NANO_ASIO_HANDLER_TRACKING)
		{
			auto timestamp = std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
			std::cout << (boost::format ("[%1%] io_thread held for %2%ms") % timestamp % timer.since_start ().count ()).str () << std::endl;
		}
		// Sleep for a bit to give more time slices to other threads
		std::this_thread::sleep_for (std::chrono::milliseconds (5));
		std::this_thread::yield ();
	}
#endif
}

void nano::thread_runner::join ()
{
	io_guard.reset ();
	for (auto & i : threads)
	{
		if (i.joinable ())
		{
			i.join ();
		}
	}
}

void nano::thread_runner::stop_event_processing ()
{
	io_guard.get_executor ().context ().stop ();
}
