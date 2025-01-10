#include <celerix/boost/process/child.hpp>
#include <celerix/lib/files.hpp>
#include <celerix/lib/signal_manager.hpp>
#include <celerix/lib/stacktrace.hpp>
#include <celerix/lib/thread_runner.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/celerix_node/daemon.hpp>
#include <celerix/node/cli.hpp>
#include <celerix/node/daemonconfig.hpp>
#include <celerix/node/ipc/ipc_server.hpp>
#include <celerix/node/json_handler.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/openclwork.hpp>
#include <celerix/rpc/rpc.hpp>

#include <boost/process.hpp>

#include <csignal>
#include <iostream>
#include <latch>

#include <fmt/chrono.h>

namespace
{
void celerix_abort_signal_handler (int signum)
{
	// remove `signum` from signal handling when under Windows
#ifdef _WIN32
	std::signal (signum, SIG_DFL);
#endif

	// create some debugging log files
	celerix::dump_crash_stacktrace ();
	celerix::create_load_memory_address_files ();

	// re-raise signal to call the default handler and exit
	raise (signum);
}

void install_abort_signal_handler ()
{
	// We catch signal SIGSEGV and SIGABRT not via the signal manager because we want these signal handlers
	// to be executed in the stack of the code that caused the signal, so we can dump the stacktrace.
#ifdef _WIN32
	std::signal (SIGSEGV, celerix_abort_signal_handler);
	std::signal (SIGABRT, celerix_abort_signal_handler);
#else
	struct sigaction sa = {};
	sa.sa_handler = celerix_abort_signal_handler;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;
	sigaction (SIGSEGV, &sa, NULL);
	sigaction (SIGABRT, &sa, NULL);
#endif
}
}

void celerix::daemon::run (std::filesystem::path const & data_path, celerix::node_flags const & flags)
{
	celerix::logger::initialize (celerix::log_config::daemon_default (), data_path, flags.config_overrides);

	logger.info (celerix::log::type::daemon, "Daemon started");

	install_abort_signal_handler ();

	std::filesystem::create_directories (data_path);
	boost::system::error_code error_chmod;
	celerix::set_secure_perm_directory (data_path, error_chmod);

	std::unique_ptr<celerix::thread_runner> runner;

	celerix::network_params network_params{ celerix::network_constants::active_network };
	celerix::daemon_config config{ data_path, network_params };
	if (auto error = celerix::read_node_config_toml (data_path, config, flags.config_overrides))
	{
		logger.critical (celerix::log::type::daemon, "Error deserializing node config: {}", error.get_message ());
		std::exit (1);
	}
	if (auto error = celerix::flags_config_conflicts (flags, config.node))
	{
		logger.critical (celerix::log::type::daemon, "Error parsing command line options: {}", error.message ());
		std::exit (1);
	}

	celerix::set_use_memory_pools (config.node.use_memory_pools);

	std::shared_ptr<boost::asio::io_context> io_ctx = std::make_shared<boost::asio::io_context> ();

	auto opencl = celerix::opencl_work::create (config.opencl_enable, config.opencl, logger, config.node.network_params.work);
	celerix::opencl_work_func_t opencl_work_func;
	if (opencl)
	{
		opencl_work_func = [&opencl] (celerix::work_version const version_a, celerix::root const & root_a, uint64_t difficulty_a, std::atomic<int> & ticket_a) {
			return opencl->generate_work (version_a, root_a, difficulty_a, ticket_a);
		};
	}
	celerix::work_pool opencl_work (config.node.network_params.network, config.node.work_threads, config.node.pow_sleep_interval, opencl_work_func);
	try
	{
		// This avoids a blank prompt during any node initialization delays
		logger.info (celerix::log::type::daemon, "Starting up Celerix node...");

		// Print info about number of logical cores detected, those are used to decide how many IO, worker and signature checker threads to spawn
		logger.info (celerix::log::type::daemon, "Hardware concurrency: {} ( configured: {} )", std::thread::hardware_concurrency (), celerix::hardware_concurrency ());
		logger.info (celerix::log::type::daemon, "File descriptors limit: {}", celerix::get_file_descriptor_limit ());

		// for the daemon start up, if the user hasn't specified a port in
		// the config, we must use the default peering port for the network
		//
		if (!config.node.peering_port.has_value ())
		{
			config.node.peering_port = network_params.network.default_node_port;
		}

		auto node = std::make_shared<celerix::node> (io_ctx, data_path, config.node, opencl_work, flags);
		if (!node->init_error ())
		{
			auto network_label = node->network_params.network.get_current_network_as_string ();
			std::time_t dateTime = std::time (nullptr);

			logger.info (celerix::log::type::daemon, "Network: {}", network_label);
			logger.info (celerix::log::type::daemon, "Version: {}", CELERIX_VERSION_STRING);
			logger.info (celerix::log::type::daemon, "Data path: '{}'", node->application_path.string ());
			logger.info (celerix::log::type::daemon, "Build info: {}", BUILD_INFO);
			logger.info (celerix::log::type::daemon, "Database backend: {}", node->store.vendor_get ());
			logger.info (celerix::log::type::daemon, "Start time: {:%c} UTC", fmt::gmtime (dateTime));

			// IO context runner should be started first and stopped last to allow asio handlers to execute during node start/stop
			runner = std::make_unique<celerix::thread_runner> (io_ctx, logger, node->config.io_threads, celerix::thread_role::name::io_daemon);

			node->start ();

			std::atomic stopped{ false };

			std::unique_ptr<celerix::ipc::ipc_server> ipc_server = std::make_unique<celerix::ipc::ipc_server> (*node, config.rpc);
			std::unique_ptr<boost::process::child> rpc_process;
			std::unique_ptr<celerix::rpc_handler_interface> rpc_handler;
			std::shared_ptr<celerix::rpc> rpc;

			if (config.rpc_enable)
			{
				// In process RPC
				if (!config.rpc.child_process.enable)
				{
					auto stop_callback = [this, &stopped] () {
						logger.warn (celerix::log::type::daemon, "RPC stop request received, stopping...");
						stopped = true;
						stopped.notify_all ();
					};

					// Launch rpc in-process
					celerix::rpc_config rpc_config{ config.node.network_params.network };
					if (auto error = celerix::read_rpc_config_toml (data_path, rpc_config, flags.rpc_config_overrides))
					{
						logger.critical (celerix::log::type::daemon, "Error deserializing RPC config: {}", error.get_message ());
						std::exit (1);
					}

					rpc_handler = std::make_unique<celerix::inprocess_rpc_handler> (*node, *ipc_server, config.rpc, stop_callback);
					rpc = celerix::get_rpc (io_ctx, rpc_config, *rpc_handler);
					rpc->start ();
				}
				else
				{
					// Spawn a child rpc process
					if (!std::filesystem::exists (config.rpc.child_process.rpc_path))
					{
						throw std::runtime_error (std::string ("RPC is configured to spawn a new process however the file cannot be found at: ") + config.rpc.child_process.rpc_path);
					}

					std::string network{ node->network_params.network.get_current_network_as_string () };
					rpc_process = std::make_unique<boost::process::child> (config.rpc.child_process.rpc_path, "--daemon", "--data_path", data_path.string (), "--network", network);
				}
				debug_assert (rpc || rpc_process);
			}

			auto signal_handler = [this, &stopped] (int signum) {
				logger.warn (celerix::log::type::daemon, "Interrupt signal received ({}), stopping...", to_signal_name (signum));
				stopped = true;
				stopped.notify_all ();
			};

			celerix::signal_manager sigman;
			// keep trapping Ctrl-C to avoid a second Ctrl-C interrupting tasks started by the first
			sigman.register_signal_handler (SIGINT, signal_handler, true);
			// sigterm is less likely to come in bunches so only trap it once
			sigman.register_signal_handler (SIGTERM, signal_handler, false);

			// Keep running until stopped flag is set
			stopped.wait (false);

			logger.info (celerix::log::type::daemon, "Stopping...");

			if (rpc)
			{
				rpc->stop ();
			}
			ipc_server->stop ();
			node->stop ();
			io_ctx->stop ();
			runner->join ();

			if (rpc_process)
			{
				rpc_process->wait ();
			}
		}
		else
		{
			logger.critical (celerix::log::type::daemon, "Error initializing node");
		}
	}
	catch (std::runtime_error const & e)
	{
		logger.critical (celerix::log::type::daemon, "Error while running node: {}", e.what ());
	}

	logger.info (celerix::log::type::daemon, "Daemon stopped");
}
