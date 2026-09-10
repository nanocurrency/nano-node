#pragma once

#include <nano/boost/process/child.hpp>
#include <nano/lib/logging.hpp>

#include <filesystem>
#include <string_view>

namespace nano
{
class node_rpc_config;

/**
 * The `nano_rpc` child spawned when RPC is configured to run in a separate process.
 * The child talks to the node over IPC. A `stop` request that came through it makes it exit on
 * its own once the node acknowledges the request; any other shutdown terminates it from here.
 */
class rpc_process final
{
public:
	// Throws `std::runtime_error` when the configured executable does not exist
	rpc_process (nano::node_rpc_config const &, std::filesystem::path const & data_path, std::string_view network, nano::logger &);

	// Gives the child a moment to exit on its own and terminates it otherwise
	void stop ();

private:
	nano::logger & logger;
	boost::process::child child;
};
}
