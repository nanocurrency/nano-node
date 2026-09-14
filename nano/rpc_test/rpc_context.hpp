#pragma once

#include <boost/property_tree/ptree.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace nano
{
class node;
class node_rpc_config;
class public_key;
class account;
class rpc_host;
}

namespace nano::ipc
{
class ipc_server;
}

namespace nano::test
{
class system;

/**
 * An RPC host wired to a node over IPC, built the same way the standalone `nano_rpc` process
 * builds it, so the tests run the production composition on its own IO threads. Only the test
 * client and the node itself are driven by the polled `system` io_context.
 */
class rpc_context
{
public:
	rpc_context () = default;
	rpc_context (rpc_context &&) = default;
	~rpc_context ();

	std::unique_ptr<nano::node_rpc_config> node_rpc_config;
	std::shared_ptr<nano::ipc::ipc_server> ipc_server;
	std::unique_ptr<nano::rpc_host> host;
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
};

rpc_context add_rpc (nano::test::system &, std::shared_ptr<nano::node> const &, rpc_options const & options = {});
}
