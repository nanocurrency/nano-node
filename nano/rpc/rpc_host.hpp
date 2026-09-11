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
 * threads they run on. `start` and `stop` encode the one order every owner needs, so the
 * standalone `nano_rpc` process, the daemon, the wallet and the test harness all run the same
 * code. A `stop` request arriving over RPC is reported to the owner by the backend; the owner
 * decides what to do about it and calls `stop` from a thread of its own.
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
	// Drains the listener, stops the IO threads and releases the backend; idempotent, never from an IO thread
	void stop ();

	// Port the listener is bound to, only meaningful after `start`
	std::uint16_t listening_port () const;

	nano::rpc_config const config;

private:
	nano::logger logger{ "rpc" };
	std::shared_ptr<boost::asio::io_context> io_ctx;
	std::unique_ptr<nano::rpc_handler_interface> backend;
	std::unique_ptr<nano::rpc_server> server;
	std::unique_ptr<nano::thread_runner> runner; // Last, so its threads are joined before anything they use goes away
	std::atomic<bool> stopped{ false };
};
}
