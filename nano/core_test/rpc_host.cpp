#include <nano/core_test/fakes/rpc_handler.hpp>
#include <nano/lib/config.hpp>
#include <nano/rpc/rpc_host.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/test_response.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

/*
 * `rpc_host` runs a listener, a backend and the IO threads for both, in the order every owner
 * uses. These tests drive it with backends that know nothing about the RPC API; the test thread
 * plays the owner.
 */

namespace
{
nano::rpc_config host_config (nano::test::system & system, uint16_t port = 0)
{
	nano::rpc_config config{ nano::dev::network_params.network, port != 0 ? port : system.get_available_port (), true };
	config.rpc_process.io_threads = 2;
	return config;
}

bool accepts_connections (uint16_t port)
{
	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::socket socket{ io_ctx };
	boost::system::error_code ec;
	socket.connect (boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::loopback (), port }, ec);
	return !ec;
}

boost::property_tree::ptree echo_request ()
{
	boost::property_tree::ptree request;
	request.put ("action", "echo");
	return request;
}

/** A raw TCP connection to the host that never sends anything */
class idle_connection
{
public:
	explicit idle_connection (uint16_t port)
	{
		boost::system::error_code ec;
		socket.connect (boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::loopback (), port }, ec);
		connected = !ec;
	}

	// True once the host has closed its end
	bool closed_by_peer ()
	{
		boost::system::error_code ec;
		socket.non_blocking (true, ec);
		std::array<char, 1> byte{};
		socket.read_some (boost::asio::buffer (byte), ec);
		return ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset;
	}

	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::socket socket{ io_ctx };
	bool connected{ false };
};
}

/**
 * Starting serves requests through the backend, stopping releases the port and is idempotent
 */
TEST (rpc_host, start_stop)
{
	nano::test::system system;
	nano::rpc_host host{ host_config (system) };
	auto backend = std::make_unique<nano::test::echo_rpc_handler> ();
	auto & requests = backend->requests;
	host.start (std::move (backend));
	ASSERT_NE (0, host.listening_port ());

	auto request = echo_request ();
	auto response = nano::test::test_response::send (request, host.listening_port (), *system.io_ctx);
	ASSERT_TIMELY_EQ (5s, response->status, 200);
	ASSERT_EQ ("echo", response->json.get<std::string> ("action"));
	ASSERT_EQ (1, requests);

	host.stop ();
	ASSERT_FALSE (accepts_connections (host.listening_port ()));
	host.stop ();
}

/**
 * A host that is destroyed without an explicit stop, started or not, shuts down cleanly
 */
TEST (rpc_host, destroy_without_stop)
{
	nano::test::system system;
	{
		nano::rpc_host host{ host_config (system) };
	}
	uint16_t port{ 0 };
	{
		nano::rpc_host host{ host_config (system) };
		host.start (std::make_unique<nano::test::echo_rpc_handler> ());
		port = host.listening_port ();
		ASSERT_TRUE (accepts_connections (port));
	}
	ASSERT_FALSE (accepts_connections (port));
}

/**
 * Starting on a taken port throws and leaves a host that can still be destroyed
 */
TEST (rpc_host, bind_failure)
{
	if (nano::is_windows_build ())
	{
		GTEST_SKIP () << "Windows lets a second acceptor with SO_REUSEADDR bind a port in use";
	}

	nano::test::system system;
	nano::rpc_host first{ host_config (system) };
	first.start (std::make_unique<nano::test::echo_rpc_handler> ());

	nano::rpc_host second{ host_config (system, first.listening_port ()) };
	ASSERT_THROW (second.start (std::make_unique<nano::test::echo_rpc_handler> ()), std::runtime_error);
}

/**
 * Connections that were accepted but never sent a request are closed by `stop` and do not delay it
 */
TEST (rpc_host, stop_closes_idle_connections)
{
	nano::test::system system;
	nano::rpc_host host{ host_config (system) };
	host.start (std::make_unique<nano::test::echo_rpc_handler> ());

	std::vector<std::unique_ptr<idle_connection>> connections;
	for (int i = 0; i < 16; ++i)
	{
		connections.push_back (std::make_unique<idle_connection> (host.listening_port ()));
		ASSERT_TRUE (connections.back ()->connected);
	}

	auto const start = std::chrono::steady_clock::now ();
	host.stop ();
	ASSERT_LT (std::chrono::steady_clock::now () - start, host.config.drain_timeout);
	ASSERT_FALSE (accepts_connections (host.listening_port ()));
	ASSERT_TIMELY (5s, std::all_of (connections.begin (), connections.end (), [] (auto const & connection) { return connection->closed_by_peer (); }));
}

/**
 * The owner's sequence, stop the listener then the IO threads, still writes a response that was
 * in flight when `stop` was called: this is the acknowledgement of a `stop` request in production
 */
TEST (rpc_host, stop_writes_pending_response)
{
	nano::test::system system;
	nano::rpc_host host{ host_config (system) };
	auto backend = std::make_unique<nano::test::deferred_rpc_handler> ();
	auto & handler = *backend;
	host.start (std::move (backend));

	auto request = echo_request ();
	auto response = nano::test::test_response::send (request, host.listening_port (), *system.io_ctx);
	ASSERT_TIMELY_EQ (5s, handler.pending_count (), 1);

	// Release the response only once the owner is already inside `stop`
	nano::test::join_guard releaser;
	releaser.spawn ([&handler] () {
		std::this_thread::sleep_for (500ms);
		handler.respond_all (R"({ "released": "1" })");
	});

	host.stop ();
	ASSERT_TIMELY_EQ (5s, response->status, 200);
	ASSERT_EQ ("1", response->json.get<std::string> ("released"));
}

/**
 * A request that never completes only delays `stop` by the drain timeout, then its connection is closed
 */
TEST (rpc_host, stop_drain_timeout)
{
	nano::test::system system;
	auto config = host_config (system);
	config.drain_timeout = 500ms;
	nano::rpc_host host{ config };
	auto backend = std::make_unique<nano::test::deferred_rpc_handler> ();
	auto & handler = *backend;
	host.start (std::move (backend));

	auto request = echo_request ();
	auto response = nano::test::test_response::send (request, host.listening_port (), *system.io_ctx);
	ASSERT_TIMELY_EQ (5s, handler.pending_count (), 1);

	auto const start = std::chrono::steady_clock::now ();
	host.stop ();
	ASSERT_GE (std::chrono::steady_clock::now () - start, 500ms);
	ASSERT_FALSE (accepts_connections (host.listening_port ()));

	// The client sees its connection dropped without a response
	ASSERT_TIMELY (5s, response->status != 0);
	ASSERT_NE (200, response->status);
}
