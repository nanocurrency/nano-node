#include <nano/lib/logging.hpp>
#include <nano/node/node_rpc_config.hpp>
#include <nano/node/rpc_process.hpp>

#include <boost/system/error_code.hpp>

#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;

nano::rpc_process::rpc_process (nano::node_rpc_config const & config, std::filesystem::path const & data_path, std::string_view network, nano::logger & logger_a) :
	logger{ logger_a },
	child{ context }
{
	auto const & path = config.child_process.rpc_path;
	if (!std::filesystem::exists (path))
	{
		throw std::runtime_error ("RPC is configured to spawn a new process however the file cannot be found at: " + path);
	}

	auto const data = data_path.string ();
	auto const network_arg = std::string (network);
	child = boost::process::v2::process (context, path, { "--daemon", "--data_path", data, "--network", network_arg });
}

void nano::rpc_process::stop ()
{
	boost::system::error_code ec;

	auto const deadline = std::chrono::steady_clock::now () + grace_period;
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
