#pragma once

#include <boost/property_tree/ptree.hpp>

namespace nano
{
class ipc_rpc_processor;
class node;
class node_rpc_config;
class public_key;
class account;
class rpc;

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
		rpc_context (std::shared_ptr<nano::rpc> & rpc_a, std::shared_ptr<nano::ipc::ipc_server> & ipc_server_a, std::unique_ptr<nano::ipc_rpc_processor> & ipc_rpc_processor_a, std::unique_ptr<nano::node_rpc_config> & node_rpc_config_a);

		std::shared_ptr<nano::rpc> rpc;
		std::shared_ptr<nano::ipc::ipc_server> ipc_server;
		std::unique_ptr<nano::ipc_rpc_processor> ipc_rpc_processor;
		std::unique_ptr<nano::node_rpc_config> node_rpc_config;
	};

	void wait_response_impl (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time, boost::property_tree::ptree & response_json);

	boost::property_tree::ptree wait_response (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time = 5s);

	bool check_block_response_count (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, uint64_t size_count);

	// Configuration for the RPC server started by add_rpc
	struct rpc_options
	{
		bool enable_control{ true };
	};

	rpc_context add_rpc (nano::test::system &, std::shared_ptr<nano::node> const &, rpc_options const & options = {});
}
}
