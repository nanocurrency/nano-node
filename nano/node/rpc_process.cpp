#include <nano/node/node_rpc_config.hpp>
#include <nano/node/rpc_process.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;

nano::rpc_process::rpc_process (nano::node_rpc_config const & config, std::filesystem::path const & data_path, std::string_view network, nano::logger & logger_a) :
	logger{ logger_a }
{
	auto const & path = config.child_process.rpc_path;
	if (!std::filesystem::exists (path))
	{
		throw std::runtime_error ("RPC is configured to spawn a new process however the file cannot be found at: " + path);
	}

	child = boost::process::child (path, "--daemon", "--data_path", data_path.string (), "--network", std::string (network));
}

void nano::rpc_process::stop ()
{
	std::error_code ec;

	auto const deadline = std::chrono::steady_clock::now () + 5s;
	while (child.running (ec) && std::chrono::steady_clock::now () < deadline)
	{
		std::this_thread::sleep_for (100ms);
	}

	if (child.running (ec))
	{
		logger.warn (nano::log::type::rpc_process, "RPC process did not exit on its own, terminating it");
		child.terminate (ec);
	}
	child.wait (ec);
}
