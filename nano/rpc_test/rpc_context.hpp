#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/property_tree/ptree.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace nano
{
class ipc_rpc_processor;
class node;
class node_rpc_config;
class public_key;
class account;
class rpc_server;
class thread_runner;

namespace ipc
{
	class ipc_server;
}

namespace test
{
	class system;

	/**
	 * An RPC server wired to a node over IPC, as the standalone `nano_rpc` process is.
	 * The server and its IPC client run on dedicated IO threads like in production; only the
	 * test client side is driven by the polled `system` io_context.
	 */
	class rpc_context
	{
	public:
		rpc_context () = default;
		rpc_context (rpc_context &&) = default;
		~rpc_context ();

		std::shared_ptr<boost::asio::io_context> io_ctx;
		std::unique_ptr<nano::node_rpc_config> node_rpc_config;
		std::shared_ptr<nano::ipc::ipc_server> ipc_server;
		std::unique_ptr<nano::ipc_rpc_processor> ipc_rpc_processor;
		std::shared_ptr<nano::rpc_server> rpc;
		// Declared last so its threads are joined before the objects they may still be using are destroyed
		std::unique_ptr<nano::thread_runner> runner;
	};

	void wait_response_impl (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time, boost::property_tree::ptree & response_json);

	boost::property_tree::ptree wait_response (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, std::chrono::duration<double, std::nano> const & time = 5s);

	void wait_responses_impl (nano::test::system & system, rpc_context const & rpc_ctx, std::vector<boost::property_tree::ptree> & requests, std::chrono::duration<double, std::nano> const & time, std::vector<boost::property_tree::ptree> & responses);

	// Sends all requests at once and waits for every response, returned in request order.
	// Concurrent processing requires the server to be started with more than one IPC connection, see rpc_options::num_ipc_connections
	std::vector<boost::property_tree::ptree> wait_responses (nano::test::system & system, rpc_context const & rpc_ctx, std::vector<boost::property_tree::ptree> & requests, std::chrono::duration<double, std::nano> const & time = 5s);

	bool check_block_response_count (nano::test::system & system, rpc_context const & rpc_ctx, boost::property_tree::ptree & request, uint64_t size_count);

	// Configuration for the RPC server started by add_rpc
	struct rpc_options
	{
		bool enable_control{ true };
		// Overrides the RPC → IPC connection count; the dev network default of 1 processes requests one at a time
		std::optional<unsigned> num_ipc_connections{};
		// Invoked when a `stop` request reaches the node over IPC and again when the RPC side sees the acknowledgement, the harness never stops anything on its own
		std::function<void ()> stop_callback{ [] () {} };
	};

	rpc_context add_rpc (nano::test::system &, std::shared_ptr<nano::node> const &, rpc_options const & options = {});
}
}
