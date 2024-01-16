#pragma once

#include <nano/lib/logging.hpp>

#include <boost/property_tree/ptree.hpp>

#include <functional>
#include <string>

namespace nano
{
class rpc_config;
class rpc_handler_interface;
class rpc_handler_request_params;

class rpc_handler : public std::enable_shared_from_this<nano::rpc_handler>
{
public:
	rpc_handler (nano::rpc_config const & rpc_config, std::string const & body_a, std::string const & request_id_a, std::function<void (std::string const &)> const & response_a, nano::rpc_handler_interface & rpc_handler_interface_a, nano::nlogger &);
	void process_request (nano::rpc_handler_request_params const & request_params);

private:
	std::string body;
	std::string request_id;
	boost::property_tree::ptree request;
	std::function<void (std::string const &)> response;
	nano::rpc_config const & rpc_config;
	nano::rpc_handler_interface & rpc_handler_interface;
	nano::nlogger & nlogger;
};
}
