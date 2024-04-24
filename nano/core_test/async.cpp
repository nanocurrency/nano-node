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