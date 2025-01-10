#pragma once

#include <celerix/boost/asio/ip/tcp.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/rpc_handler_interface.hpp>
#include <celerix/lib/rpcconfig.hpp>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace celerix
{
class rpc_handler_interface;

class rpc : public std::enable_shared_from_this<rpc>
{
public:
	rpc (std::shared_ptr<boost::asio::io_context>, celerix::rpc_config config_a, celerix::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc ();

	void start ();
	void stop ();

	virtual void accept ();

	std::uint16_t listening_port () const
	{
		return acceptor.local_endpoint ().port ();
	}

public:
	celerix::logger logger{ "rpc" };
	celerix::rpc_config config;
	std::shared_ptr<boost::asio::io_context> io_ctx_shared;
	boost::asio::io_context & io_ctx;
	boost::asio::ip::tcp::acceptor acceptor;
	celerix::rpc_handler_interface & rpc_handler_interface;
	bool stopped{ false };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::shared_ptr<celerix::rpc> get_rpc (std::shared_ptr<boost::asio::io_context>, celerix::rpc_config const & config_a, celerix::rpc_handler_interface & rpc_handler_interface_a);
}
