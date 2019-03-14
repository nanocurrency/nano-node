#pragma once

#include <boost/property_tree/ptree.hpp>
#include <functional>
#include <string>

namespace nano
{
void error_response (std::function<void(std::string const &)> response_a, std::string const & message_a);

namespace ipc
{
	class ipc_client;
}

class rpc_handler : public std::enable_shared_from_this<nano::rpc_handler>
{
public:
	rpc_handler (nano::ipc::ipc_client & ipc_client, std::function<void()> stop_callback, std::string const & body_a, std::string const & request_id_a, std::function<void(std::string const &)> const & response_a);
	void process_request (unsigned max_json_depth, bool enable_control);

	std::string body;
	std::string request_id;
	boost::property_tree::ptree request;
	std::function<void(std::string const &)> response;
	nano::ipc::ipc_client & ipc_client;
	std::function<void()> stop_callback;
};
}
