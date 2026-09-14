#pragma once

#include <nano/lib/rpc_handler_interface.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace nano::test
{
/** Echoes every request body back, so the server side can be tested without any RPC API */
class echo_rpc_handler final : public nano::rpc_handler_interface
{
public:
	void process_request (std::string const &, std::string const & body, std::function<void (std::string const &)> response) override
	{
		++requests;
		response (body);
	}

	void process_request_v2 (nano::rpc_handler_request_params const &, std::string const & body, std::function<void (std::shared_ptr<std::string> const &)> response) override
	{
		++requests;
		response (std::make_shared<std::string> (body));
	}

	void stop () override
	{
	}

	void rpc_instance (nano::rpc_server &) override
	{
	}

	std::atomic<int> requests{ 0 };
};
}
