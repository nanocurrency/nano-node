#pragma once

#include <boost/property_tree/ptree.hpp>
#include <functional>
#include <string>

namespace nano
{
void error_response (std::function<void(std::string const &)> response_a, std::string const & message_a);
class rpc_request_processor;

namespace ipc
{
	class ipc_client;
}

class rpc_handler : public std::enable_shared_from_this<nano::rpc_handler>
{
public:
	rpc_handler (nano::rpc_config const & rpc_config, std::string const & body_a, std::string const & request_id_a, std::function<void(std::string const &)> const & response_a, nano::rpc_request_processor & rpc_request_processor);
	void process_request ();
	void read (std::shared_ptr<std::vector<uint8_t>> req, std::shared_ptr<std::vector<uint8_t>> res, const std::string & action);

	std::string body;
	std::string request_id;
	boost::property_tree::ptree request;
	std::function<void(std::string const &)> response;
	nano::rpc_config const & rpc_config;
	nano::rpc_request_processor & rpc_request_processor;
};
}
