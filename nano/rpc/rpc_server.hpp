#pragma once

#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/rpc_handler_interface.hpp>
#include <nano/lib/rpcconfig.hpp>

#include <atomic>

namespace nano
{
class rpc_handler_interface;

class rpc_server : public std::enable_shared_from_this<rpc_server>
{
public:
	rpc_server (std::shared_ptr<boost::asio::io_context>, nano::rpc_config config_a, nano::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc_server ();

	void start ();
	void stop ();

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
	std::atomic<bool> stopped{ false };
	std::uint16_t port{ 0 };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::shared_ptr<nano::rpc_server> get_rpc (std::shared_ptr<boost::asio::io_context>, nano::rpc_config const & config_a, nano::rpc_handler_interface & rpc_handler_interface_a);
}
