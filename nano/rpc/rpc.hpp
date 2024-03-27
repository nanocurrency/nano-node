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

class rpc : public std::enable_shared_from_this<rpc>
{
public:
	rpc (std::shared_ptr<boost::asio::io_context>, nano::rpc_config config_a, nano::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc ();

	void start ();
	void stop ();

	virtual void accept ();

	std::uint16_t listening_port () const
	{
		return acceptor.local_endpoint ().port ();
	}

public:
	nano::logger logger{ "rpc" };
	nano::rpc_config config;
	std::shared_ptr<boost::asio::io_context> io_ctx_shared;
	boost::asio::io_context & io_ctx;
	boost::asio::ip::tcp::acceptor acceptor;
	nano::rpc_handler_interface & rpc_handler_interface;
	bool stopped{ false };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<nano::rpc> get_rpc (std::shared_ptr<boost::asio::io_context>, nano::rpc_config const & config_a, nano::rpc_handler_interface & rpc_handler_interface_a);
}
