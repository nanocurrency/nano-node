#include <nano/lib/async.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
class test_context
{
public:
	std::shared_ptr<asio::io_context> io_ctx{ std::make_shared<asio::io_context> () };
	nano::logger logger;
	nano::thread_runner runner{ io_ctx, logger, 1 };
	nano::async::strand strand{ io_ctx->get_executor () };
};

// Runs a coroutine through settle on a context of its own until it ends
template <typename Result>
nano::async::settled run_settled (asio::awaitable<Result> body)
{
	asio::io_context io_ctx;
	auto fut = asio::co_spawn (io_ctx, nano::async::settle (std::move (body)), asio::use_future);
	io_ctx.run ();
	return fut.get ();
}
}

TEST (async, sleep)
{
	test_context ctx;

	auto fut = asio::co_spawn (
	ctx.strand,
	[&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (500ms);
	},
	asio::use_future);

	ASSERT_EQ (fut.wait_for (100ms), std::future_status::timeout);
	ASSERT_EQ (fut.wait_for (1s), std::future_status::ready);
}

TEST (async, cancellation)
{
	test_context ctx;

	nano::async::cancellation cancellation{ ctx.strand };

	auto fut = asio::co_spawn (
	ctx.strand,
	[&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (10s);
	},
	asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));

	ASSERT_EQ (fut.wait_for (500ms), std::future_status::timeout);

	cancellation.emit ();

	ASSERT_EQ (fut.wait_for (1s), std::future_status::ready);
	ASSERT_NO_THROW (fut.get ());
}

// Test that cancellation signal behaves well when the cancellation is emitted after the task has completed
TEST (async, cancellation_lifetime)
{
	test_context ctx;

	nano::async::cancellation cancellation{ ctx.strand };
	{
		auto fut = asio::co_spawn (
		ctx.strand,
		[&] () -> asio::awaitable<void> {
			co_await nano::async::sleep_for (100ms);
		},
		asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));
		ASSERT_EQ (fut.wait_for (1s), std::future_status::ready);
		fut.get ();
	}
	auto cancel_fut = cancellation.emit ();
	ASSERT_EQ (cancel_fut.wait_for (1s), std::future_status::ready);
}

TEST (async, task)
{
	nano::test::system system;
	test_context ctx;

	nano::async::task task{ ctx.strand };

	// Default state, empty task
	ASSERT_FALSE (task.joinable ());
	ASSERT_FALSE (task.running ());

	task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (500ms);
	});

	// Task should now be joinable, but not ready
	ASSERT_TRUE (task.joinable ());
	ASSERT_TRUE (task.running ());
	ASSERT_FALSE (task.ready ());

	WAIT (50ms);
	ASSERT_TRUE (task.joinable ());
	ASSERT_TRUE (task.running ());
	ASSERT_FALSE (task.ready ());

	WAIT (1s);

	// Task completed, not yet joined
	ASSERT_TRUE (task.joinable ());
	ASSERT_FALSE (task.running ());
	ASSERT_TRUE (task.ready ());

	task.join ();

	// It is allowed to join a task multiple times
	ASSERT_TRUE (task.joinable ());
	task.join ();
}

TEST (async, task_cancel)
{
	nano::test::system system;
	test_context ctx;

	nano::async::task task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (10s);
	});

	// Task should be joinable, but not ready
	WAIT (100ms);
	ASSERT_TRUE (task.joinable ());
	ASSERT_TRUE (task.running ());
	ASSERT_FALSE (task.ready ());

	task.cancel ();

	WAIT (500ms);
	ASSERT_TRUE (task.joinable ());
	ASSERT_TRUE (task.ready ());

	// It should not be necessary to join a ready task
}

TEST (async, task_join)
{
	nano::test::system system;
	test_context ctx;

	nano::async::task task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (500ms);
	});

	ASSERT_FALSE (task.ready ());
	ASSERT_TRUE (task.running ());
	ASSERT_TRUE (task.joinable ());

	task.join ();
	ASSERT_TRUE (task.ready ());
	ASSERT_FALSE (task.running ());

	// It is allowed to join a task multiple times
	ASSERT_TRUE (task.joinable ());
	task.join ();
}

TEST (async, task_join_multithread)
{
	nano::test::system system;
	test_context ctx;

	nano::async::task task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (500ms);
	});

	ASSERT_FALSE (task.ready ());
	ASSERT_TRUE (task.joinable ());

	const int howmany_threads = 5;

	std::vector<std::thread> threads;
	for (int i = 0; i < howmany_threads; ++i)
	{
		threads.emplace_back ([&task] () {
			task.join ();
		});
	}

	// Join all threads
	for (auto & thread : threads)
	{
		thread.join ();
	}

	// Threads should be able to join the task without throwing an exception

	// It is allowed to join a task multiple times
	ASSERT_TRUE (task.joinable ());
	task.join ();
}

/*
 * Move assigning over a running task would detach its coroutine from the only handle that can cancel or join it, so the task refuses it
 */
TEST (async_DeathTest, move_assign_running)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	nano::test::system system;
	test_context ctx;

	// A task that keeps running for the whole test
	nano::async::task task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (10s);
	});
	ASSERT_TRUE (task.running ());

	// Overwriting it with a fresh task must abort instead of losing the running coroutine
	ASSERT_DEATH ({
		task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
			co_return;
		});
	},
	"async task not joined before move assignment");

	// The parent process still owns the running task and ends it the proper way
	task.cancel ();
	task.join ();
}

/*
 * Destroying a task while its coroutine still runs would leave the coroutine without a handle, so the destructor refuses it
 */
TEST (async_DeathTest, destroy_running)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	nano::test::system system;
	test_context ctx;

	// A running task that goes out of scope must abort instead of being detached
	ASSERT_DEATH ({
		nano::async::task task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
			co_await nano::async::sleep_for (10s);
		});
	},
	"async task not joined before destruction");
}

/*
 * A task that was never given a coroutine has no future to inspect or wait on, so ready () and join () refuse it
 */
TEST (async_DeathTest, empty_task)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	nano::test::system system;
	test_context ctx;

	// The default constructed task is not joinable
	nano::async::task task{ ctx.strand };
	ASSERT_FALSE (task.joinable ());

	// Both operations that need the future must abort
	ASSERT_DEATH ({ task.ready (); }, "future.valid \\(\\)");
	ASSERT_DEATH ({ task.join (); }, "future.valid \\(\\)");
}

/*
 * Joining from a thread of the task's own io_context would wait for a completion that needs that thread, so join () refuses it
 */
TEST (async_DeathTest, join_on_io_thread)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	nano::test::system system;
	test_context ctx;

	// A task that stays running so the join would actually wait
	nano::async::task task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (10s);
	});
	ASSERT_TRUE (task.running ());

	// Joining from inside the io_context must abort rather than deadlock its only thread
	ASSERT_DEATH ({
		auto fut = asio::post (ctx.strand, asio::use_future ([&] () {
			task.join ();
		}));
		fut.wait ();
	},
	"async task joined on a thread of its io_context");

	// The parent process ends the task the proper way
	task.cancel ();
	task.join ();
}

/*
 * Joining after the task's io_context stopped would wait for a completion that can no longer run, so join () refuses it
 */
TEST (async_DeathTest, join_after_io_context_stopped)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	nano::test::system system;
	test_context ctx;

	// A task that stays running so the join would actually wait
	nano::async::task task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (10s);
	});
	ASSERT_TRUE (task.running ());

	// Stopping the context and joining its threads leaves the coroutine suspended for good, so joining must abort
	ASSERT_DEATH ({
		ctx.runner.abort ();
		ctx.runner.join ();
		task.join ();
	},
	"async task joined after its io_context stopped");

	// The parent process still runs the context and ends the task the proper way
	task.cancel ();
	task.join ();
}

/*
 * A condition that was moved from has no shared state, so notifying, cancelling or waiting on it refuses instead of dereferencing nothing
 */
TEST (async_DeathTest, condition_moved_from)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	nano::test::system system;
	test_context ctx;

	// Moving the condition leaves the source without state
	nano::async::condition source{ ctx.strand };
	nano::async::condition moved{ std::move (source) };
	ASSERT_FALSE (source.valid ());
	ASSERT_TRUE (moved.valid ());

	// Every operation that needs the state must abort on the source, the wait only once its coroutine runs on the strand
	ASSERT_DEATH ({ source.notify (); }, "Assertion `state` failed");
	ASSERT_DEATH ({ source.cancel (); }, "Assertion `state` failed");
	ASSERT_DEATH ({
		auto fut = asio::co_spawn (ctx.strand, source.wait_for (1s), asio::use_future);
		fut.wait ();
	},
	"Assertion `state` failed");
}

/*
 * A finished task has nothing left to wait for, so joining it still works after its io_context ran out of work and stopped, and keeps working on every repeat
 */
TEST (async, task_join_completed_after_stop)
{
	// A context run on this thread without a work guard, so it stops by itself once the task is done
	asio::io_context io_ctx;
	nano::async::strand strand{ io_ctx.get_executor () };

	nano::async::task task = nano::async::task (strand, [] () -> asio::awaitable<void> {
		co_return;
	});

	// Running the context drains the task and leaves the context stopped
	io_ctx.run ();
	ASSERT_TRUE (io_ctx.stopped ());
	ASSERT_TRUE (task.ready ());

	// Joining returns at once, however many times it is asked
	task.join ();
	task.join ();
}

/*
 * A finished task has nothing left to wait for, so joining it from an io thread of its own context returns instead of refusing
 */
TEST (async, task_join_completed_on_io_thread)
{
	test_context ctx;

	// A task that finishes right away, joined from the test thread first so it is known to be done
	nano::async::task task = nano::async::task (ctx.strand, [] () -> asio::awaitable<void> {
		co_return;
	});
	task.join ();
	ASSERT_TRUE (task.ready ());

	// Joining again from a handler on the task's strand completes the handler normally
	auto fut = asio::post (ctx.strand, asio::use_future ([&] () {
		task.join ();
	}));
	ASSERT_EQ (fut.wait_for (5s), std::future_status::ready);
	ASSERT_NO_THROW (fut.get ());
}

/*
 * A coroutine that ends on its own settles with the error it returned, empty on success, and nothing unexpected
 */
TEST (async, settle_returned)
{
	// A body that succeeds settles with an empty code
	auto const succeeded = run_settled ([] () -> asio::awaitable<nano::io_result<void>> {
		co_return nano::io_success ();
	}());
	ASSERT_FALSE (succeeded.ec);
	ASSERT_FALSE (succeeded.unexpected);

	// A body that fails settles with the code it returned
	auto const failed = run_settled ([] () -> asio::awaitable<nano::io_result<void>> {
		co_return boost::system::error_code{ asio::error::connection_reset };
	}());
	ASSERT_EQ (asio::error::connection_reset, failed.ec);
	ASSERT_FALSE (failed.unexpected);

	// A body without a result settles with an empty code
	auto const finished = run_settled ([] () -> asio::awaitable<void> {
		co_return;
	}());
	ASSERT_FALSE (finished.ec);
	ASSERT_FALSE (finished.unexpected);
}

/*
 * A cancelled coroutine returns the operation_aborted its wait ended with, which settles as that error and not as something unexpected
 */
TEST (async, settle_cancelled)
{
	asio::io_context io_ctx;
	asio::cancellation_signal signal;

	// A body returning how a wait that would not end during the test went, spawned with a slot the test cancels through
	auto fut = asio::co_spawn (io_ctx, nano::async::settle (nano::async::sleep_for (1h)), asio::bind_cancellation_slot (signal.slot (), asio::use_future));

	// Run until the body waits on the timer, then cancel it and run to the end
	io_ctx.poll ();
	signal.emit (asio::cancellation_type::terminal);
	io_ctx.run ();

	// The cancellation is reported as operation_aborted
	auto const ended = fut.get ();
	ASSERT_EQ (asio::error::operation_aborted, ended.ec);
	ASSERT_FALSE (ended.unexpected);
}

/*
 * Anything a coroutine throws settles as unexpected, with its message and an error code, instead of escaping to the caller
 */
TEST (async, settle_unexpected)
{
	// An exception without an error code settles with error_common::exception and its message
	auto const thrown = run_settled ([] () -> asio::awaitable<nano::io_result<void>> {
		throw std::runtime_error ("test exception");
		co_return nano::io_success ();
	}());
	ASSERT_EQ (std::error_code{ nano::error_common::exception }, thrown.ec);
	ASSERT_EQ ("test exception", thrown.unexpected);

	// A system error keeps its own code
	auto const system_error = run_settled ([] () -> asio::awaitable<void> {
		throw boost::system::system_error{ asio::error::connection_reset };
		co_return;
	}());
	ASSERT_EQ (asio::error::connection_reset, system_error.ec);
	ASSERT_TRUE (system_error.unexpected);

	// A thrown cancellation is unexpected like any other error, since a cancelled wait returns it
	auto const cancellation = run_settled ([] () -> asio::awaitable<void> {
		throw boost::system::system_error{ asio::error::operation_aborted };
		co_return;
	}());
	ASSERT_EQ (asio::error::operation_aborted, cancellation.ec);
	ASSERT_TRUE (cancellation.unexpected);

	// So does a standard library system error, which is not the type asio throws
	auto const std_system_error = run_settled ([] () -> asio::awaitable<void> {
		throw std::system_error{ std::make_error_code (std::errc::timed_out) };
		co_return;
	}());
	ASSERT_EQ (std::make_error_code (std::errc::timed_out), std_system_error.ec);
	ASSERT_TRUE (std_system_error.unexpected);

	// A thrown value that is not an exception class is caught as well
	auto const unknown = run_settled ([] () -> asio::awaitable<void> {
		throw 42;
		co_return;
	}());
	ASSERT_EQ (std::error_code{ nano::error_common::exception }, unknown.ec);
	ASSERT_EQ ("unknown exception", unknown.unexpected);
}

/*
 * sleep_for reports how the wait ended: success once the duration passes, operation_aborted when the coroutine is cancelled first.
 * A caller that loops around the sleep needs the cancellation as a value to leave its loop
 */
TEST (async, sleep_for_result)
{
	test_context ctx;

	// A short sleep runs out and succeeds
	auto finished = asio::co_spawn (ctx.strand, nano::async::sleep_for (1ms), asio::use_future);
	ASSERT_EQ (finished.wait_for (5s), std::future_status::ready);
	ASSERT_FALSE (finished.get ().has_error ());

	// A long sleep cancelled before it runs out returns the cancellation instead of throwing it
	nano::async::cancellation cancellation{ ctx.strand };
	auto cancelled = asio::co_spawn (ctx.strand, nano::async::sleep_for (10s), asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));
	ASSERT_EQ (cancelled.wait_for (100ms), std::future_status::timeout);
	cancellation.emit ();
	ASSERT_EQ (cancelled.wait_for (5s), std::future_status::ready);
	nano::io_result<void> result;
	ASSERT_NO_THROW (result = cancelled.get ());
	ASSERT_EQ (asio::error::operation_aborted, result.error ());
}

/*
 * Cancelling a task throws nothing into its coroutine: the wait it is suspended in returns operation_aborted, cancelled () turns true and the coroutine runs on to its own end
 */
TEST (async, task_cancel_not_thrown)
{
	test_context ctx;

	// What the body saw, written on the strand and read once the task is joined
	nano::io_result<void> slept;
	bool cancelled{ false };
	bool finished{ false };

	// A body that waits far longer than the test runs and keeps awaiting once that wait ends
	auto body = [] (nano::io_result<void> & slept, bool & cancelled, bool & finished) -> asio::awaitable<void> {
		slept = co_await nano::async::sleep_for (10s);
		cancelled = co_await nano::async::cancelled ();
		finished = true;
	};
	nano::async::task task{ ctx.strand, body (slept, cancelled, finished) };

	// Cancel once the body is waiting
	ASSERT_EQ (task.get_future ().wait_for (100ms), std::future_status::timeout);
	task.cancel ();
	task.join ();

	// The wait reported the cancellation and the body went on, where a thrown cancellation would have skipped the rest
	ASSERT_EQ (asio::error::operation_aborted, slept.error ());
	ASSERT_TRUE (cancelled);
	ASSERT_TRUE (finished);
}

/*
 * A cancellation is delivered once, to the wait the task is suspended in at that moment, so a wait started afterwards would run in full.
 * sleep_for looks for an earlier cancellation before it starts and returns operation_aborted at once
 */
TEST (async, sleep_for_after_cancel)
{
	test_context ctx;

	// How each wait ended, written on the strand and read once the task is joined
	nano::io_result<void> first;
	nano::io_result<void> second;

	// A body that starts another wait after its first one was cancelled
	auto body = [] (nano::io_result<void> & first, nano::io_result<void> & second) -> asio::awaitable<void> {
		first = co_await nano::async::sleep_for (10s);
		second = co_await nano::async::sleep_for (1s);
	};
	nano::async::task task{ ctx.strand, body (first, second) };

	// Cancel once the body is in its first wait
	ASSERT_EQ (task.get_future ().wait_for (100ms), std::future_status::timeout);
	task.cancel ();
	task.join ();

	// Both waits report the cancellation, the second without having waited
	ASSERT_EQ (asio::error::operation_aborted, first.error ());
	ASSERT_EQ (asio::error::operation_aborted, second.error ());
}
