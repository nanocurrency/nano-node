#pragma once

#include <boost/property_tree/ptree.hpp>

namespace celerix
{
class ipc_rpc_processor;
class node;
class node_rpc_config;
class public_key;
class rpc;
using account = public_key;

namespace ipc
{
	class ipc_server;
}

namespace test
{
	class system;
	class rpc_context
	{
	public:
		rpc_context (std::shared_ptr<celerix::rpc> & rpc_a, std::shared_ptr<celerix::ipc::ipc_server> & ipc_server_a, std::unique_ptr<celerix::ipc_rpc_processor> & ipc_rpc_processor_a, std::unique_ptr<celerix::node_rpc_config> & node_rpc_config_a);

		std::shared_ptr<celerix::rpc> rpc;
		std::shared_ptr<celerix::ipc::ipc_server> ipc_server;
		std::unique_ptr<celerix::ipc_rpc_processor> ipc_rpc_processor;
		std::unique_ptr<celerix::node_rpc_config> node_rpc_config;
	};

	void wait_response_impl (celerix::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::celerix> const & time, boost::property_tree::ptree & response_json);

	boost::property_tree::ptree wait_response (celerix::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::celerix> const & time = 5s);

	bool check_block_response_count (celerix::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, uint64_t size_count);
	rpc_context add_rpc (celerix::test::system & system, std::shared_ptr<celerix::node> const & node_a);
}
}
