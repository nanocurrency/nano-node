#pragma once

#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/rpc_handler_interface.hpp>
#include <nano/lib/rpcconfig.hpp>

#include <atomic>
#include <chrono>

namespace nano
{
class rpc_connection_tracker;
class rpc_handler_interface;

class rpc_server : public std::enable_shared_from_this<rpc_server>
{
public:
	rpc_server (std::shared_ptr<boost::asio::io_context>, nano::rpc_config config_a, nano::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc_server ();

	void start ();
	void stop ();

	// Lets the accept in flight settle, closes idle connections, waits up to `timeout` for the requests in flight to finish and closes the rest; for the owner's thread, after `stop`, while the IO threads run
	void drain (std::chrono::milliseconds timeout);

	virtual void accept ();

	// Port the acceptor was bound to, only meaningful after `start`
	std::uint16_t listening_port () const;

public:
	nano::logger logger{ "rpc" };
	nano::rpc_config config;
	std::shared_ptr<boost::asio::io_context> io_ctx_shared;
	boost::asio::io_context & io_ctx;
	boost::asio::ip::tcp::acceptor acceptor;
	nano::rpc_handler_interface & rpc_handler_interface;
	std::shared_ptr<nano::rpc_connection_tracker> connections;
	nano::mutex mutex; // Guards the acceptor, the accept in flight and the registration of accepted connections
	nano::condition_variable condition; // Signals a settled accept to `drain`
	std::size_t accepting{ 0 }; // Accept operations in flight, at most one
	std::atomic<bool> stopped{ false };
	std::uint16_t port{ 0 };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::shared_ptr<nano::rpc_server> get_rpc (std::shared_ptr<boost::asio::io_context>, nano::rpc_config const & config_a, nano::rpc_handler_interface & rpc_handler_interface_a);
}
