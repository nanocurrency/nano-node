#include <nano/lib/async.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST (async, sleep)
{
	auto io_ctx = std::make_shared<asio::io_context> ();
	nano::thread_runner runner{ io_ctx, 1 };
	nano::async::strand strand{ io_ctx->get_executor () };

	auto fut = asio::co_spawn (
	strand,
	[&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (500ms);
	},
	asio::use_future);

	ASSERT_EQ (fut.wait_for (100ms), std::future_status::timeout);
	ASSERT_EQ (fut.wait_for (1s), std::future_status::ready);
}

TEST (async, cancellation)
{
	auto io_ctx = std::make_shared<asio::io_context> ();
	nano::thread_runner runner{ io_ctx, 1 };
	nano::async::strand strand{ io_ctx->get_executor () };

	nano::async::cancellation cancellation{ strand };

	auto fut = asio::co_spawn (
	strand,
	[&] () -> asio::awaitable<void> {
		co_await nano::async::sleep_for (10s);
	},
	asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));

	ASSERT_EQ (fut.wait_for (500ms), std::future_status::timeout);

	cancellation.emit ();

	ASSERT_EQ (fut.wait_for (500ms), std::future_status::ready);
	ASSERT_NO_THROW (fut.get ());
}

TEST (async, task)
{
	nano::test::system system;

	auto io_ctx = std::make_shared<asio::io_context> ();
	nano::thread_runner runner{ io_ctx, 1 };
	nano::async::strand strand{ io_ctx->get_executor () };

	nano::async::task task{ strand };

	// Default state, empty task
	ASSERT_FALSE (task.joinable ());

	task = nano::async::spawn (strand, [&] () -> asio::awaitable<void> {
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

	auto io_ctx = std::make_shared<asio::io_context> ();
	nano::thread_runner runner{ io_ctx, 1 };
	nano::async::strand strand{ io_ctx->get_executor () };

	nano::async::task task = nano::async::spawn (strand, [&] () -> asio::awaitable<void> {
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