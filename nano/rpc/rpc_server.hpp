#pragma once

#include <nano/lib/async.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/rpcconfig.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <list>
#include <memory>

namespace nano
{
class rpc_connection;
class rpc_handler_interface;

/**
 * Tracks the connections a server has accepted and how many of them are serving a request,
 * so that stopping the server can close the idle ones and wait for the rest to finish.
 * Shared between the server and its connections, since connections may outlive the server.
 */
class rpc_connection_tracker final
{
public:
	void add (std::shared_ptr<nano::rpc_connection> const &);
	void request_begin ();
	void request_end ();

	std::size_t in_flight () const;
	// Closes every connection that is not serving a request
	void close_idle ();
	// Closes every connection, including those still serving a request
	void close_all ();
	// Waits until no request is in flight, returns false if the timeout expires first
	bool wait_drained (std::chrono::milliseconds timeout);

private:
	void purge ();

	mutable nano::mutex mutex;
	nano::condition_variable condition;
	std::size_t in_flight_m{ 0 };
	std::list<std::weak_ptr<nano::rpc_connection>> connections;
};

/**
 * Accepts HTTP connections for the RPC API and hands every request to a `rpc_handler_interface`.
 *
 * The accept loop is a task running on a strand owned by this object, so the acceptor is only
 * touched from that strand while the loop runs. `stop` cancels and joins the loop, closes the
 * acceptor, closes the connections that are not serving a request and waits for the ones that
 * are, so every response that was started is written before `stop` returns. Connections still
 * serving a request when `drain_timeout` expires are closed. `stop` is idempotent and may be
 * called from any thread that is not running this server's `io_context`, since joining from
 * such a thread could deadlock.
 */
class rpc_server final
{
public:
	rpc_server (std::shared_ptr<boost::asio::io_context>, nano::rpc_config, nano::rpc_handler_interface &);
	~rpc_server ();

	void start ();
	void stop ();

	// Port the acceptor is bound to, only meaningful after `start`
	std::uint16_t listening_port () const;

	nano::rpc_config const config;
	// How long `stop` waits for requests in flight before closing their connections
	std::chrono::milliseconds drain_timeout{ std::chrono::seconds{ 5 } };

private:
	asio::awaitable<void> run ();

	nano::logger logger{ "rpc" };
	nano::rpc_handler_interface & handler;
	std::shared_ptr<boost::asio::io_context> io_ctx;
	nano::async::strand strand;
	boost::asio::ip::tcp::acceptor acceptor;
	std::shared_ptr<nano::rpc_connection_tracker> connections{ std::make_shared<nano::rpc_connection_tracker> () };
	nano::async::task task;
	std::atomic<bool> stopped{ false };
	std::uint16_t port{ 0 };
};
}
