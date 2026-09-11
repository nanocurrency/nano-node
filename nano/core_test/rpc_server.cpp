#include <nano/lib/locks.hpp>
#include <nano/lib/rpc_handler_interface.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/rpc/rpc_server.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/test_response.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

/*
 * `rpc_server` only accepts connections and hands requests to a handler, so these tests drive it
 * with handlers that know nothing about the RPC API. The server runs on its own IO threads, the
 * way every owner runs it, and the test thread plays the owner: it is the only one calling `stop`.
 */

namespace
{
/** Echoes every request body back */
class echo_handler final : public nano::rpc_handler_interface
{
public:
	void process_request (std::string const &, std::string const & body, std::function<void (std::string const &)> response) override
	{
		++requests;
		response (body);
	}

	void process_request_v2 (nano::rpc_handler_request_params const &, std::string const & body, std::function<void (std::shared_ptr<std::string> const &)> response) override
	{
		++requests;
		response (std::make_shared<std::string> (body));
	}

	std::atomic<int> requests{ 0 };
};

/** Holds every request until the test releases it, to keep connections in flight */
class deferred_handler final : public nano::rpc_handler_interface
{
public:
	void process_request (std::string const &, std::string const &, std::function<void (std::string const &)> response) override
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		pending.push_back (std::move (response));
	}

	void process_request_v2 (nano::rpc_handler_request_params const &, std::string const &, std::function<void (std::shared_ptr<std::string> const &)> response) override
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		pending.push_back ([response = std::move (response)] (std::string const & body) {
			response (std::make_shared<std::string> (body));
		});
	}

	std::size_t pending_count ()
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return pending.size ();
	}

	void respond_all (std::string const & body)
	{
		decltype (pending) released;
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			released.swap (pending);
		}
		for (auto & response : released)
		{
			response (body);
		}
	}

private:
	nano::mutex mutex;
	std::deque<std::function<void (std::string const &)>> pending;
};

/**
 * An RPC server with its handler on its own IO threads, torn down in the same order the owners use.
 * Members are declared so that the runner's threads are joined first and the io_context dies last,
 * after the handler has released any response it still holds and the connections behind it.
 */
template <class handler_type>
class server_context
{
public:
	server_context (nano::test::system & system, uint16_t port) :
		config{ nano::dev::network_params.network, port, true },
		server{ io_ctx, config, handler },
		runner{ io_ctx, system.logger, 2 }
	{
	}

	~server_context ()
	{
		server.stop ();
		runner.abort ();
	}

	std::shared_ptr<boost::asio::io_context> io_ctx{ std::make_shared<boost::asio::io_context> () };
	handler_type handler;
	nano::rpc_config config;
	nano::rpc_server server;
	nano::thread_runner runner;
};

/** A raw TCP connection to the server that never sends anything */
class idle_connection
{
public:
	explicit idle_connection (uint16_t port)
	{
		boost::system::error_code ec;
		socket.connect (boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::loopback (), port }, ec);
		connected = !ec;
	}

	// True once the server has closed its end
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

bool accepts_connections (uint16_t port)
{
	return idle_connection{ port }.connected;
}

boost::property_tree::ptree echo_request ()
{
	boost::property_tree::ptree request;
	request.put ("action", "echo");
	return request;
}
}

/**
 * Starting binds a port and a request round-trips through the handler, stopping releases the port
 */
TEST (rpc_server, start_stop)
{
	nano::test::system system;
	server_context<echo_handler> ctx{ system, system.get_available_port () };

	ctx.server.start ();
	ASSERT_NE (0, ctx.server.listening_port ());
	ASSERT_TRUE (accepts_connections (ctx.server.listening_port ()));

	auto request = echo_request ();
	auto response = nano::test::test_response::send (request, ctx.server.listening_port (), *system.io_ctx);
	ASSERT_TIMELY_EQ (5s, response->status, 200);
	ASSERT_EQ ("echo", response->json.get<std::string> ("action"));
	ASSERT_EQ (1, ctx.handler.requests);

	ctx.server.stop ();
	ASSERT_FALSE (accepts_connections (ctx.server.listening_port ()));
}

/**
 * Stopping is idempotent, before or after starting, and a server that was never started can be destroyed
 */
TEST (rpc_server, stop_idempotent)
{
	nano::test::system system;
	{
		server_context<echo_handler> ctx{ system, system.get_available_port () };
		// Never started
	}
	{
		server_context<echo_handler> ctx{ system, system.get_available_port () };
		ctx.server.stop ();
		ctx.server.stop ();
	}
	{
		server_context<echo_handler> ctx{ system, system.get_available_port () };
		ctx.server.start ();
		auto const port = ctx.server.listening_port ();
		ctx.server.stop ();
		ctx.server.stop ();
		ASSERT_FALSE (accepts_connections (port));
	}
}

/**
 * A stopped server has released its port, so a new server can bind it
 */
TEST (rpc_server, stop_releases_port)
{
	nano::test::system system;
	uint16_t port{ 0 };
	{
		server_context<echo_handler> ctx{ system, system.get_available_port () };
		ctx.server.start ();
		port = ctx.server.listening_port ();
	}

	server_context<echo_handler> ctx{ system, port };
	ASSERT_NO_THROW (ctx.server.start ());
	ASSERT_EQ (port, ctx.server.listening_port ());

	auto request = echo_request ();
	auto response = nano::test::test_response::send (request, port, *system.io_ctx);
	ASSERT_TIMELY_EQ (5s, response->status, 200);
}

/**
 * Starting on a port that is already taken reports the failure to the owner as an exception
 */
TEST (rpc_server, bind_failure)
{
	nano::test::system system;
	server_context<echo_handler> first{ system, system.get_available_port () };
	first.server.start ();

	server_context<echo_handler> second{ system, first.server.listening_port () };
	ASSERT_THROW (second.server.start (), std::runtime_error);
}

/**
 * Many clients issuing requests from different threads at once are all served
 */
TEST (rpc_server, concurrent_requests)
{
	nano::test::system system;
	server_context<echo_handler> ctx{ system, system.get_available_port () };
	ctx.server.start ();

	constexpr int count = 64;
	auto request = echo_request ();
	std::vector<std::shared_ptr<nano::test::test_response>> responses;
	for (int i = 0; i < count; ++i)
	{
		responses.push_back (nano::test::test_response::prepare (request, *system.io_ctx));
	}

	nano::test::join_guard threads;
	for (auto & response : responses)
	{
		threads.spawn ([&response, port = ctx.server.listening_port ()] () {
			response->run (port);
		});
	}

	ASSERT_TIMELY (10s, std::all_of (responses.begin (), responses.end (), [] (auto const & response) { return response->status != 0; }));
	for (auto const & response : responses)
	{
		ASSERT_EQ (200, response->status);
		ASSERT_EQ ("echo", response->json.get<std::string> ("action"));
	}
	ASSERT_EQ (count, ctx.handler.requests);
}

/**
 * Connections that were accepted but never sent a request are closed by `stop` and do not delay it
 */
TEST (rpc_server, stop_closes_idle_connections)
{
	nano::test::system system;
	server_context<echo_handler> ctx{ system, system.get_available_port () };
	ctx.server.start ();

	std::vector<std::unique_ptr<idle_connection>> connections;
	for (int i = 0; i < 16; ++i)
	{
		connections.push_back (std::make_unique<idle_connection> (ctx.server.listening_port ()));
		ASSERT_TRUE (connections.back ()->connected);
	}

	ctx.server.stop ();
	ASSERT_FALSE (accepts_connections (ctx.server.listening_port ()));
	ASSERT_TIMELY (5s, std::all_of (connections.begin (), connections.end (), [] (auto const & connection) { return connection->closed_by_peer (); }));
}

/**
 * `stop` waits for a request in flight, so its response is written before `stop` returns
 */
TEST (rpc_server, stop_drains_pending_request)
{
	nano::test::system system;
	server_context<deferred_handler> ctx{ system, system.get_available_port () };
	ctx.server.start ();

	auto request = echo_request ();
	auto response = nano::test::test_response::send (request, ctx.server.listening_port (), *system.io_ctx);
	ASSERT_TIMELY_EQ (5s, ctx.handler.pending_count (), 1);

	// Release the response only once the owner is already waiting inside `stop`
	std::atomic<bool> released{ false };
	nano::test::join_guard releaser;
	releaser.spawn ([&] () {
		std::this_thread::sleep_for (500ms);
		released = true;
		ctx.handler.respond_all (R"({ "released": "1" })");
	});

	ctx.server.stop ();
	ASSERT_TRUE (released);
	ASSERT_FALSE (accepts_connections (ctx.server.listening_port ()));

	ASSERT_TIMELY_EQ (5s, response->status, 200);
	ASSERT_EQ ("1", response->json.get<std::string> ("released"));
}

/**
 * A request that never completes only delays `stop` by the drain timeout, then its connection is closed
 */
TEST (rpc_server, stop_drain_timeout)
{
	nano::test::system system;
	server_context<deferred_handler> ctx{ system, system.get_available_port () };
	ctx.server.drain_timeout = 500ms;
	ctx.server.start ();

	auto request = echo_request ();
	auto response = nano::test::test_response::send (request, ctx.server.listening_port (), *system.io_ctx);
	ASSERT_TIMELY_EQ (5s, ctx.handler.pending_count (), 1);

	auto const start = std::chrono::steady_clock::now ();
	ctx.server.stop ();
	ASSERT_GE (std::chrono::steady_clock::now () - start, 500ms);
	ASSERT_FALSE (accepts_connections (ctx.server.listening_port ()));

	// The client sees its connection dropped without a response
	ASSERT_TIMELY (5s, response->status != 0);
	ASSERT_NE (200, response->status);
}

/**
 * Stopping while clients keep connecting closes the acceptor without racing the accept loop
 */
TEST (rpc_server, stop_during_connects)
{
	nano::test::system system;
	server_context<echo_handler> ctx{ system, system.get_available_port () };
	ctx.server.start ();
	auto const port = ctx.server.listening_port ();

	std::atomic<int> accepted{ 0 };
	nano::test::join_guard threads;
	for (int i = 0; i < 4; ++i)
	{
		threads.spawn ([&] (nano::test::stop_token const & stop) {
			while (!stop.stop_requested ())
			{
				if (idle_connection{ port }.connected)
				{
					++accepted;
				}
			}
		});
	}

	ASSERT_TIMELY (5s, accepted >= 50);
	ctx.server.stop ();

	ASSERT_FALSE (accepts_connections (port));
}

/**
 * Concurrent `stop` calls from several owner threads are safe, only one of them closes the acceptor
 */
TEST (rpc_server, stop_concurrent)
{
	nano::test::system system;
	server_context<echo_handler> ctx{ system, system.get_available_port () };
	ctx.server.start ();
	auto const port = ctx.server.listening_port ();

	nano::test::join_guard connectors;
	connectors.spawn ([&] (nano::test::stop_token const & stop) {
		while (!stop.stop_requested ())
		{
			idle_connection connection{ port };
		}
	});

	{
		nano::test::join_guard stoppers;
		for (int i = 0; i < 8; ++i)
		{
			stoppers.spawn ([&] () {
				ctx.server.stop ();
			});
		}
	}

	ASSERT_FALSE (accepts_connections (port));
}
