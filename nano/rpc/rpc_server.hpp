#pragma once

#include <nano/lib/async.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/rpcconfig.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace nano
{
class rpc_handler_interface;

/**
 * Accepts HTTP connections for the RPC API and hands every request to a `rpc_handler_interface`.
 *
 * The accept loop is a task running on a strand owned by this object, so the acceptor is only
 * touched from that strand while the loop runs. `stop` cancels and joins the loop before closing
 * the acceptor; it is idempotent and may be called from any thread that is not running this
 * server's `io_context`, since joining from such a thread could deadlock. Connections that were
 * already accepted are not interrupted, they finish their request on their own.
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

private:
	asio::awaitable<void> run ();

	nano::logger logger{ "rpc" };
	nano::rpc_handler_interface & handler;
	std::shared_ptr<boost::asio::io_context> io_ctx;
	nano::async::strand strand;
	boost::asio::ip::tcp::acceptor acceptor;
	nano::async::task task;
	std::atomic<bool> stopped{ false };
	std::uint16_t port{ 0 };
};
}
