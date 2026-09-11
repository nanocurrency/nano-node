#include <nano/core_test/fakes/rpc_handler.hpp>
#include <nano/rpc/rpc_host.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/test_response.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/property_tree/ptree.hpp>

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
	nano::test::system system;
	nano::rpc_host first{ host_config (system) };
	first.start (std::make_unique<nano::test::echo_rpc_handler> ());

	nano::rpc_host second{ host_config (system, first.listening_port ()) };
	ASSERT_THROW (second.start (std::make_unique<nano::test::echo_rpc_handler> ()), std::runtime_error);
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
