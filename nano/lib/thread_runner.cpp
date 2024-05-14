#include <nano/lib/thread_runner.hpp>
#include <nano/lib/timer.hpp>

#include <iostream>
#include <thread>

/*
 * thread_runner
 */

nano::thread_runner::thread_runner (std::shared_ptr<asio::io_context> io_ctx_a, nano::logger & logger_a, unsigned num_threads_a, const nano::thread_role::name thread_role_a) :
	num_threads{ num_threads_a },
	role{ thread_role_a },
	logger{ logger_a },
	io_ctx{ std::move (io_ctx_a) },
	io_guard{ asio::make_work_guard (*io_ctx) }
{
	debug_assert (io_ctx != nullptr);
	start ();
}

nano::thread_runner::~thread_runner ()
{
	join ();
}

void nano::thread_runner::start ()
{
	logger.debug (nano::log::type::thread_runner, "Starting threads: {} ({})", num_threads, to_string (role));

	for (auto i = 0; i < num_threads; ++i)
	{
		threads.emplace_back (nano::thread_attributes::get_default (), [this] () {
			nano::thread_role::set (role);
			try
			{
				run ();
			}
			catch (std::exception const & ex)
			{
				logger.critical (nano::log::type::thread_runner, "Error: {}", ex.what ());

#ifndef NDEBUG
				throw; // Re-throw to debugger in debug mode
#endif
			}
			catch (...)
			{
				logger.critical (nano::log::type::thread_runner, "Unknown error");

#ifndef NDEBUG
				throw; // Re-throw to debugger in debug mode
#endif
			}
		});
	}
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
	threads.clear ();

	logger.debug (nano::log::type::thread_runner, "Stopped threads ({})", to_string (role));

	io_ctx.reset (); // Release shared_ptr to io_context
}

void nano::thread_runner::abort ()
{
	release_assert (io_ctx != nullptr);
	io_ctx->stop ();
}

void nano::thread_runner::run ()
{
	if constexpr (nano::asio_handler_tracking_threshold () == 0)
	{
		io_ctx->run ();
	}
	else
	{
		nano::timer<> timer;
		timer.start ();
		while (true)
		{
			timer.restart ();
			// Run at most 1 completion handler and record the time it took to complete (non-blocking)
			auto count = io_ctx->poll_one ();
			if (count == 1 && timer.since_start ().count () >= nano::asio_handler_tracking_threshold ())
			{
				logger.warn (nano::log::type::system, "Async handler processing took too long: {}ms", timer.since_start ().count ());
			}
			// Sleep for a bit to give more time slices to other threads
			std::this_thread::sleep_for (std::chrono::milliseconds (5));
			std::this_thread::yield ();
		}
	}
}
