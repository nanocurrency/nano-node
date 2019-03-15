#pragma once

#include <algorithm>
#include <boost/beast.hpp>
#include <nano/lib/ipc_client.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/lib/timer.hpp>
#include <nano/rpc/rpc_connection.hpp>
#include <nano/rpc/rpc_handler.hpp>

namespace nano
{
void error_response (std::function<void(std::string const &)> response_a, std::string const & message_a);

class rpc
{
public:
	rpc (boost::asio::io_context & io_ctx_a, nano::rpc_config const & config_a, nano::ipc::ipc_client & ipc_client);
	void start ();
	void accept ();
	void stop ();

	nano::rpc_config config;
	nano::ipc::ipc_client & ipc_client;
	boost::asio::ip::tcp::acceptor acceptor;
	nano::logger_mt logger;
	boost::asio::io_context & io_ctx;
	nano::network_params network_params;
};
}
