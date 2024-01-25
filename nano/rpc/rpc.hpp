#pragma once

#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/rpc_handler_interface.hpp>
#include <nano/lib/rpcconfig.hpp>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace nano
{
class rpc_handler_interface;

class rpc
{
public:
	rpc (boost::asio::io_context & io_ctx_a, nano::rpc_config config_a, nano::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc ();
	void start ();
	virtual void accept ();
	void stop ();

	std::uint16_t listening_port ()
	{
		return acceptor.local_endpoint ().port ();
	}

	nano::logger logger{ "rpc" };
	nano::rpc_config config;
	boost::asio::ip::tcp::acceptor acceptor;
	boost::asio::io_context & io_ctx;
	nano::rpc_handler_interface & rpc_handler_interface;
	bool stopped{ false };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<nano::rpc> get_rpc (boost::asio::io_context & io_ctx_a, nano::rpc_config const & config_a, nano::rpc_handler_interface & rpc_handler_interface_a);
}
