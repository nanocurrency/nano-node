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

	task = nano::async::task (ctx.strand, [&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (500ms);
	});

	// Task should now be joinable, but not ready
	ASSERT_TRUE (task.joinable ());
	ASSERT_FALSE (task.ready ());

	WAIT (50ms);
	ASSERT_TRUE (task.joinable ());
	ASSERT_FALSE (task.ready ());

	WAIT (1s);

	// Task completed, not yet joined
	ASSERT_TRUE (task.joinable ());
	ASSERT_TRUE (task.ready ());

	task.join ();

	ASSERT_FALSE (task.joinable ());
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
	ASSERT_FALSE (task.ready ());

	task.cancel ();

	WAIT (500ms);
	ASSERT_TRUE (task.joinable ());
	ASSERT_TRUE (task.ready ());

	// It should not be necessary to join a ready task
}