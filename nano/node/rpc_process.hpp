#pragma once

#include <nano/boost/process/child.hpp>
#include <nano/node/fwd.hpp>

#include <chrono>
#include <filesystem>
#include <string_view>

namespace nano
{
/**
 * The `nano_rpc` child spawned when RPC is configured to run in a separate process.
 * The child serves RPC on its own and talks to the node over IPC. Stopping gives it a moment
 * to exit on its own, as it does when both processes were signalled together, and terminates
 * it otherwise, so the owner never waits on it indefinitely.
 */
class rpc_process final
{
public:
	// How long `stop` waits for the child to exit on its own before terminating it
	static constexpr std::chrono::seconds grace_period{ 5 };

public:
	// Spawns the child, throws `std::runtime_error` when the configured executable does not exist
	rpc_process (nano::node_rpc_config const &, std::filesystem::path const & data_path, std::string_view network, nano::logger &);

	// Waits up to `grace_period` for the child to exit on its own, then terminates it
	void stop ();

private:
	nano::logger & logger;
	boost::process::child child;
};
}
