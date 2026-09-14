#pragma once

#include <nano/lib/logging.hpp>
#include <nano/lib/rpcconfig.hpp>

#include <boost/asio/io_context.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace nano
{
class rpc_handler_interface;
class rpc_server;
class thread_runner;

/**
 * Hosts an RPC endpoint: the HTTP listener, the backend that serves its requests and the IO
 * threads both run on. `start` brings them up and `stop` takes them down in a fixed order, so
 * the standalone `nano_rpc` process, the daemon, the wallet and the test harness share one
 * shutdown sequence. `stop` blocks until the IO threads have exited, so calling it from one of
 * them, for example from a request handler, would deadlock; owners call it from their own thread.
 */
class rpc_host final
{
public:
	explicit rpc_host (nano::rpc_config);
	~rpc_host ();

	// The io_context a backend must use for its own asynchronous work
	std::shared_ptr<boost::asio::io_context> const & io_context () const;

	// Binds the listener and serves requests through `backend`, throws if the port cannot be bound
	void start (std::unique_ptr<nano::rpc_handler_interface> backend);
	// Closes the listener, stops the IO threads and releases the backend; idempotent
	void stop ();

	// Port the listener is bound to, only meaningful after `start`
	std::uint16_t listening_port () const;

	nano::rpc_config const config;

private:
	nano::logger logger{ "rpc" };
	std::shared_ptr<boost::asio::io_context> io_ctx;
	std::unique_ptr<nano::rpc_handler_interface> backend;
	std::shared_ptr<nano::rpc_server> server;
	std::unique_ptr<nano::thread_runner> runner; // Last, so its threads are joined before anything they use goes away
	std::atomic<bool> stopped{ false };
};
}
