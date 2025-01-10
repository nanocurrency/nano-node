#pragma once

#include <celerix/lib/logging.hpp>

#include <boost/property_tree/ptree.hpp>

#include <functional>
#include <string>

namespace celerix
{
class rpc_config;
class rpc_handler_interface;
class rpc_handler_request_params;

class rpc_handler : public std::enable_shared_from_this<celerix::rpc_handler>
{
public:
	rpc_handler (celerix::rpc_config const & rpc_config, std::string const & body_a, std::string const & request_id_a, std::function<void (std::string const &)> const & response_a, celerix::rpc_handler_interface & rpc_handler_interface_a, celerix::logger &);
	void process_request (celerix::rpc_handler_request_params const & request_params);

private:
	std::string body;
	std::string request_id;
	boost::property_tree::ptree request;
	std::function<void (std::string const &)> response;
	celerix::rpc_config const & rpc_config;
	celerix::rpc_handler_interface & rpc_handler_interface;
	celerix::logger & logger;
};
}
