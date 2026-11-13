#include <nano/lib/async.hpp>
#include <nano/lib/io_result.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/thread_runner.hpp>

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <array>
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

// A loopback endpoint nothing listens on
asio::ip::tcp::endpoint closed_endpoint ()
{
	asio::io_context io_ctx;
	asio::ip::tcp::acceptor acceptor{ io_ctx, asio::ip::tcp::endpoint{ asio::ip::address_v6::loopback (), 0 } };
	return acceptor.local_endpoint ();
}
}

/*
 * An operation awaited with `use_result` hands back its value in a `nano::io_result`: the accepted socket for an accept,
 * the byte count for a read, and a valueless success for a connect and a write
 */
TEST (io_result, use_result_value)
{
	test_context ctx;
	asio::ip::tcp::acceptor acceptor{ ctx.strand, asio::ip::tcp::endpoint{ asio::ip::address_v6::loopback (), 0 } };
	auto const endpoint = acceptor.local_endpoint ();

	auto fut = asio::co_spawn (
	ctx.strand,
	[&] () -> asio::awaitable<void> {
		// A client connects and sends three bytes
		asio::co_spawn (
		ctx.strand,
		[endpoint] () -> asio::awaitable<void> {
			asio::ip::tcp::socket client{ co_await asio::this_coro::executor };
			nano::io_result<void> const connected = co_await client.async_connect (endpoint, nano::async::use_result);
			EXPECT_TRUE (connected);
			nano::io_result<size_t> const written = co_await asio::async_write (client, asio::buffer ("abc", 3), nano::async::use_result);
			EXPECT_TRUE (written);
		},
		asio::detached);

		// The accept yields the server's end of that connection; value () throws on a failure, which ends the coroutine and fails the future
		nano::io_result<asio::ip::tcp::socket> accepted = co_await acceptor.async_accept (nano::async::use_result);
		auto & socket = accepted.value ();

		// A read of exactly what was sent yields its byte count
		std::array<char, 3> data{};
		nano::io_result<size_t> const read = co_await asio::async_read (socket, asio::buffer (data), nano::async::use_result);
		EXPECT_EQ (3, read.value ());
		EXPECT_EQ ('c', data[2]);
	},
	asio::use_future);

	ASSERT_EQ (fut.wait_for (5s), std::future_status::ready);
	ASSERT_NO_THROW (fut.get ());
}

/*
 * A failed operation awaited with `use_result` hands back its error code in the `nano::io_result` instead of throwing it,
 * which is the whole point of the token
 */
TEST (io_result, use_result_error)
{
	test_context ctx;
	auto const endpoint = closed_endpoint ();

	auto fut = asio::co_spawn (
	ctx.strand,
	[&] () -> asio::awaitable<nano::io_result<void>> {
		asio::ip::tcp::socket socket{ ctx.strand };
		co_return co_await socket.async_connect (endpoint, nano::async::use_result);
	},
	asio::use_future);

	ASSERT_EQ (fut.wait_for (5s), std::future_status::ready);

	// The refused connect arrives as a value, not as an exception from the future
	nano::io_result<void> result;
	ASSERT_NO_THROW (result = fut.get ());
	ASSERT_TRUE (result.has_error ());
	ASSERT_EQ (asio::error::connection_refused, result.error ());
}

/*
 * Cancelling a coroutine that waits on an operation awaited with `use_result` completes that operation with `operation_aborted` in its result,
 * and the coroutine's next `co_await` throws as asio does for every cancelled coroutine. A wrapped handler that lost the cancellation slot
 * would leave the wait running for its full ten seconds instead
 */
TEST (io_result, use_result_cancel)
{
	test_context ctx;
	nano::async::cancellation cancellation{ ctx.strand };
	asio::steady_timer timer{ ctx.strand, 10s };

	std::atomic<bool> aborted{ false };
	std::atomic<bool> thrown{ false };
	auto fut = asio::co_spawn (
	ctx.strand,
	[&] () -> asio::awaitable<void> {
		nano::io_result<void> const waited = co_await timer.async_wait (nano::async::use_result);
		aborted = waited.has_error () && waited.error () == asio::error::operation_aborted;
		try
		{
			co_await timer.async_wait (nano::async::use_result);
		}
		catch (boost::system::system_error const & ex)
		{
			thrown = ex.code () == asio::error::operation_aborted;
		}
	},
	asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));

	// Still waiting before the cancellation
	ASSERT_EQ (fut.wait_for (100ms), std::future_status::timeout);

	cancellation.emit ();

	ASSERT_EQ (fut.wait_for (1s), std::future_status::ready);
	ASSERT_TRUE (aborted);
	ASSERT_TRUE (thrown);
}

/*
 * `as_result` adapts other completion tokens than `use_awaitable` too, here a future that receives the `nano::io_result`
 */
TEST (io_result, as_result_future)
{
	test_context ctx;
	asio::steady_timer timer{ ctx.strand, 10ms };

	std::future<nano::io_result<void>> fut = timer.async_wait (nano::async::as_result (asio::use_future));

	ASSERT_EQ (fut.wait_for (5s), std::future_status::ready);
	ASSERT_TRUE (fut.get ());
}
