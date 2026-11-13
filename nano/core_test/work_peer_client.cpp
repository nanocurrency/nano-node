#include <nano/lib/async.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/node/work_peer_client.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/fake_work_peer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <chrono>
#include <future>

using namespace std::chrono_literals;

namespace
{
// An io_context with its threads, a client on it, and the request every test asks for
class client_context final
{
public:
	std::shared_ptr<asio::io_context> io_ctx{ std::make_shared<asio::io_context> () };
	nano::logger logger;
	nano::thread_runner runner{ io_ctx, logger, /* threads */ 2 };
	nano::async::strand strand{ io_ctx->get_executor () };
	// Time to connect that leaves room for a refused connection, which takes a couple of seconds on some systems
	nano::work_peer_client client{ nano::dev::network_params.work, { .connect = 5s, .cancel = 2s }, logger };
	nano::work_request request{ .root = nano::root{ 1 }, .difficulty = nano::dev::network_params.work.base };

	// Asks the peer for work with the context's request and waits for the answer
	nano::work_peer_response generate (nano::work_peer const & peer, std::chrono::milliseconds timeout = 10s)
	{
		auto future = asio::co_spawn (strand, client.generate (peer, request, std::chrono::steady_clock::now () + timeout), asio::use_future);
		EXPECT_EQ (std::future_status::ready, future.wait_for (30s));
		return future.get ();
	}
};

// The address of a peer that listens on the IPv4 loopback address
asio::ip::tcp::endpoint loopback (uint16_t port)
{
	return asio::ip::tcp::endpoint{ asio::ip::address_v4::loopback (), port };
}
}

/*
 * A peer that answers with work reaching the difficulty.
 * The exchange succeeds and returns that work
 */
TEST (work_peer_client, generate_success)
{
	client_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };

	// Ask the peer for work, on behalf of an account
	ctx.request.account = nano::account{ 2 };
	auto const response = ctx.generate (peer.peer ());

	// The work it returned reaches the requested difficulty, and the exchange names the address that took the request
	ASSERT_EQ (nano::work_peer_status::success, response.status);
	ASSERT_EQ (loopback (peer.port ()), response.sent_to);
	ASSERT_GE (nano::dev::network_params.work.difficulty (ctx.request.version, ctx.request.root, response.work), ctx.request.difficulty);
	ASSERT_EQ (1, peer.generate_count ());

	// The account went out with the request
	ASSERT_EQ (1, peer.account_count ());
}

/*
 * A peer listed by an IPv4-mapped IPv6 address or by a host name with several addresses.
 * Both forms reach the peer, which listens on the IPv4 loopback address only, and the exchange names that address as the one that took the request
 */
TEST (work_peer_client, generate_address_forms)
{
	client_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };

	// The mapped form is connected to as the IPv4 address it stands for
	auto const mapped = ctx.generate (nano::work_peer{ "::ffff:127.0.0.1", peer.port () });
	ASSERT_EQ (nano::work_peer_status::success, mapped.status);
	ASSERT_EQ (loopback (peer.port ()), mapped.sent_to);

	// The name is looked up, and the addresses it has are tried until one takes the connection
	auto const named = ctx.generate (nano::work_peer{ "localhost", peer.port () });
	ASSERT_EQ (nano::work_peer_status::success, named.status);
	ASSERT_EQ (loopback (peer.port ()), named.sent_to);

	ASSERT_EQ (2, peer.generate_count ());

	// A request without an account names none
	ASSERT_EQ (0, peer.account_count ());
}

/*
 * A peer that answers with an HTTP error status.
 * The exchange reports it as a failure of its own
 */
TEST (work_peer_client, generate_http_error)
{
	client_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::http_error };

	auto const response = ctx.generate (peer.peer ());

	ASSERT_EQ (nano::work_peer_status::http_error, response.status);
	ASSERT_TRUE (response.sent_to.has_value ());
}

/*
 * Peers whose answer is no usable work: a body that is not JSON, work that is not a number, an error, and work below the difficulty.
 * Each one is told apart, and none of them passes for work
 */
TEST (work_peer_client, generate_bad_answers)
{
	client_context ctx;

	// A body that is not JSON
	nano::test::fake_work_peer malformed{ *ctx.io_ctx, nano::test::fake_work_peer_mode::malformed };
	ASSERT_EQ (nano::work_peer_status::malformed, ctx.generate (malformed.peer ()).status);

	// JSON whose work field is not a number
	nano::test::fake_work_peer garbled{ *ctx.io_ctx, nano::test::fake_work_peer_mode::garbled };
	ASSERT_EQ (nano::work_peer_status::malformed, ctx.generate (garbled.peer ()).status);

	// An error in place of work
	nano::test::fake_work_peer refusing{ *ctx.io_ctx, nano::test::fake_work_peer_mode::refusing };
	ASSERT_EQ (nano::work_peer_status::refused, ctx.generate (refusing.peer ()).status);

	// Work that does not reach the requested difficulty
	nano::test::fake_work_peer insufficient{ *ctx.io_ctx, nano::test::fake_work_peer_mode::insufficient };
	ASSERT_EQ (nano::work_peer_status::insufficient, ctx.generate (insufficient.peer ()).status);
}

/*
 * A peer that takes the request and never answers.
 * The exchange ends at its deadline with a timeout, and reports that the request went out
 */
TEST (work_peer_client, generate_timeout)
{
	client_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::silent };

	// Give the peer half a second to answer
	auto const start = std::chrono::steady_clock::now ();
	auto const response = ctx.generate (peer.peer (), 500ms);
	auto const elapsed = std::chrono::steady_clock::now () - start;

	// The exchange waited for the deadline and not much longer
	ASSERT_EQ (nano::work_peer_status::timeout, response.status);
	ASSERT_TRUE (response.sent_to.has_value ());
	ASSERT_GE (elapsed, 500ms);
	ASSERT_LT (elapsed, 10s);
	ASSERT_EQ (1, peer.generate_count ());
}

/*
 * A peer nothing listens at, and a name that does not resolve.
 * Both end as unreachable without a request having gone out, the name at the latest when the time to connect runs out
 */
TEST (work_peer_client, generate_unreachable)
{
	client_context ctx;

	// Find a port nothing listens on by closing a listening socket
	uint16_t closed_port{ 0 };
	{
		asio::ip::tcp::acceptor acceptor{ *ctx.io_ctx, asio::ip::tcp::endpoint{ asio::ip::address_v4::loopback (), /* any port */ 0 } };
		closed_port = acceptor.local_endpoint ().port ();
	}

	// The connection is refused
	auto const refused = ctx.generate (nano::work_peer{ "127.0.0.1", closed_port });
	ASSERT_EQ (nano::work_peer_status::unreachable, refused.status);
	ASSERT_FALSE (refused.sent_to.has_value ());

	// The lookup fails, or is given up when the time to connect is over
	auto const start = std::chrono::steady_clock::now ();
	auto const unresolved = ctx.generate (nano::work_peer{ "work-peer.invalid", closed_port });
	auto const elapsed = std::chrono::steady_clock::now () - start;
	ASSERT_TRUE (unresolved.status == nano::work_peer_status::unreachable || unresolved.status == nano::work_peer_status::timeout);
	ASSERT_FALSE (unresolved.sent_to.has_value ());
	ASSERT_LT (elapsed, 10s);
}

/*
 * An exchange that is cancelled while it waits for a silent peer.
 * It ends right away as cancelled instead of waiting for its deadline
 */
TEST (work_peer_client, generate_cancelled)
{
	nano::test::system system;
	client_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::silent };

	// Start an exchange that would wait a minute for the answer
	nano::async::cancellation cancellation{ ctx.strand };
	auto future = asio::co_spawn (ctx.strand, ctx.client.generate (peer.peer (), ctx.request, std::chrono::steady_clock::now () + 60s), asio::bind_cancellation_slot (cancellation.slot (), asio::use_future));

	// Wait until the peer has the request, then cancel
	ASSERT_TIMELY_EQ (5s, peer.generate_count (), 1);
	cancellation.emit ();

	// The exchange ends without throwing
	ASSERT_EQ (std::future_status::ready, future.wait_for (5s));
	auto const response = future.get ();
	ASSERT_EQ (nano::work_peer_status::cancelled, response.status);
	ASSERT_TRUE (response.sent_to.has_value ());
}

/*
 * A `work_cancel` sent to a peer.
 * The peer receives it for the root, and a peer that cannot be reached is given up on quietly
 */
TEST (work_peer_client, cancel)
{
	client_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };

	// Send the cancel and wait for the exchange to end
	auto future = asio::co_spawn (ctx.strand, ctx.client.cancel (peer.peer (), loopback (peer.port ()), ctx.request.root), asio::use_future);
	ASSERT_EQ (std::future_status::ready, future.wait_for (5s));

	// The peer counted a cancel and no generate
	ASSERT_EQ (1, peer.cancel_count ());
	ASSERT_EQ (0, peer.generate_count ());

	// A cancel for a peer nothing listens at ends without an error reaching the caller
	uint16_t closed_port{ 0 };
	{
		asio::ip::tcp::acceptor acceptor{ *ctx.io_ctx, asio::ip::tcp::endpoint{ asio::ip::address_v4::loopback (), /* any port */ 0 } };
		closed_port = acceptor.local_endpoint ().port ();
	}
	auto unreachable = asio::co_spawn (ctx.strand, ctx.client.cancel (nano::work_peer{ "127.0.0.1", closed_port }, loopback (closed_port), ctx.request.root), asio::use_future);
	ASSERT_EQ (std::future_status::ready, unreachable.wait_for (5s));
	ASSERT_NO_THROW (unreachable.get ());
}

/*
 * A `work_cancel` for a peer listed by a name that does not resolve, sent to the address that took the request.
 * It reaches the peer at that address, since the name is not looked up again
 */
TEST (work_peer_client, cancel_to_address_reached)
{
	client_context ctx;
	nano::test::fake_work_peer peer{ *ctx.io_ctx, nano::test::fake_work_peer_mode::good };

	// Send the cancel and wait for the exchange to end
	nano::work_peer const named{ "work-peer.invalid", peer.port () };
	auto future = asio::co_spawn (ctx.strand, ctx.client.cancel (named, loopback (peer.port ()), ctx.request.root), asio::use_future);
	ASSERT_EQ (std::future_status::ready, future.wait_for (5s));

	// The peer counted the cancel
	ASSERT_EQ (1, peer.cancel_count ());
}
