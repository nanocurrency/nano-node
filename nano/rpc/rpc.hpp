#pragma once

#include <algorithm>
#include <boost/beast.hpp>
#include <nano/lib/ipc_client.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/lib/timer.hpp>
#include <nano/rpc/rpc_connection.hpp>
#include <nano/rpc/rpc_handler.hpp>
#include <nano/rpc/rpc_request_processor.hpp>

namespace nano
{
void error_response (std::function<void(std::string const &)> response_a, std::string const & message_a);

class rpc final
{
public:
	rpc (boost::asio::io_context & io_ctx_a, nano::rpc_config const & config_a);
	~rpc();
	void start ();
	void accept ();
	void stop ();

	nano::rpc_config config;
	boost::asio::ip::tcp::acceptor acceptor;
	nano::logger_mt logger;
	boost::asio::io_context & io_ctx;
	nano::network_constants network_constants;
	nano::rpc_request_processor rpc_request_processor;
	std::atomic<bool> stopped {false};
};
}
