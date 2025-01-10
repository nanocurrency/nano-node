#include <celerix/lib/thread_runner.hpp>
#include <celerix/lib/timer.hpp>

#include <iostream>
#include <thread>

/*
 * thread_runner
 */

celerix::thread_runner::thread_runner (std::shared_ptr<asio::io_context> io_ctx_a, celerix::logger & logger_a, unsigned num_threads_a, const celerix::thread_role::name thread_role_a) :
	num_threads{ num_threads_a },
	role{ thread_role_a },
	logger{ logger_a },
	io_ctx{ std::move (io_ctx_a) },
	io_guard{ asio::make_work_guard (*io_ctx) }
{
	debug_assert (io_ctx != nullptr);
	start ();
}

celerix::thread_runner::~thread_runner ()
{
	join ();
}

void celerix::thread_runner::start ()
{
	logger.debug (celerix::log::type::thread_runner, "Starting threads: {} ({})", num_threads, to_string (role));

	for (auto i = 0; i < num_threads; ++i)
	{
		threads.emplace_back (celerix::thread_attributes::get_default (), [this] () {
			celerix::thread_role::set (role);
			try
			{
				run ();
			}
			catch (std::exception const & ex)
			{
				logger.critical (celerix::log::type::thread_runner, "Error: {}", ex.what ());

#ifndef NDEBUG
				throw; // Re-throw to debugger in debug mode
#endif
			}
			catch (...)
			{
				logger.critical (celerix::log::type::thread_runner, "Unknown error");

#ifndef NDEBUG
				throw; // Re-throw to debugger in debug mode
#endif
			}
		});
	}
}

void celerix::thread_runner::join ()
{
	io_guard.reset ();

	for (auto & thread : threads)
	{
		if (thread.joinable ())
		{
			logger.debug (celerix::log::type::thread_runner, "Joining thread: {}", fmt::streamed (thread.get_id ()));
			thread.join ();
		}
	}

	threads.clear ();

	logger.debug (celerix::log::type::thread_runner, "Stopped all threads ({})", to_string (role));

	io_ctx.reset (); // Release shared_ptr to io_context
}

void celerix::thread_runner::abort ()
{
	release_assert (io_ctx != nullptr);
	io_ctx->stop ();
}

void celerix::thread_runner::run ()
{
	if constexpr (celerix::asio_handler_tracking_threshold () == 0)
	{
		io_ctx->run ();
	}
	else
	{
		celerix::timer<> timer;
		timer.start ();
		while (true)
		{
			timer.restart ();
			// Run at most 1 completion handler and record the time it took to complete (non-blocking)
			auto count = io_ctx->poll_one ();
			if (count == 1 && timer.since_start ().count () >= celerix::asio_handler_tracking_threshold ())
			{
				logger.warn (celerix::log::type::system, "Async handler processing took too long: {}ms", timer.since_start ().count ());
			}
			// Sleep for a bit to give more time slices to other threads
			std::this_thread::sleep_for (std::chrono::milliseconds (5));
			std::this_thread::yield ();
		}
	}
}
