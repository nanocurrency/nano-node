#include <nano/boost/process/child.hpp>
#include <nano/lib/signal_manager.hpp>
#include <nano/lib/stacktrace.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/tlsconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/nano_node/daemon.hpp>
#include <nano/node/cli.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/json_handler.hpp>
#include <nano/node/node.hpp>
#include <nano/node/openclwork.hpp>
#include <nano/rpc/rpc.hpp>

#include <boost/format.hpp>
#include <boost/process.hpp>

#include <csignal>
#include <iostream>

#include <fmt/chrono.h>

namespace
{
void nano_abort_signal_handler (int signum)
{
	// remove `signum` from signal handling when under Windows
#ifdef _WIN32
	std::signal (signum, SIG_DFL);
#endif

	// create some debugging log files
	nano::dump_crash_stacktrace ();
	nano::create_load_memory_address_files ();

	// re-raise signal to call the default handler and exit
	raise (signum);
}

void install_abort_signal_handler ()
{
	// We catch signal SIGSEGV and SIGABRT not via the signal manager because we want these signal handlers
	// to be executed in the stack of the code that caused the signal, so we can dump the stacktrace.
#ifdef _WIN32
	std::signal (SIGSEGV, nano_abort_signal_handler);
	std::signal (SIGABRT, nano_abort_signal_handler);
#else
	struct sigaction sa = {};
	sa.sa_handler = nano_abort_signal_handler;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;
	sigaction (SIGSEGV, &sa, NULL);
	sigaction (SIGABRT, &sa, NULL);
#endif
}

volatile sig_atomic_t sig_int_or_term = 0;

constexpr std::size_t OPEN_FILE_DESCRIPTORS_LIMIT = 16384;
}

void nano::daemon::run (std::filesystem::path const & data_path, nano::node_flags const & flags)
{
	nano::nlogger::initialize (nano::load_log_config (nano::log_config::daemon_default (), data_path, flags.config_overrides));

	nlogger.info (nano::log::type::daemon, "Daemon started");

	install_abort_signal_handler ();

	std::filesystem::create_directories (data_path);
	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (data_path, error_chmod);

	std::unique_ptr<nano::thread_runner> runner;
	nano::network_params network_params{ nano::network_constants::active_network };
	nano::daemon_config config{ data_path, network_params };
	auto error = nano::read_node_config_toml (data_path, config, flags.config_overrides);

	nano::set_use_memory_pools (config.node.use_memory_pools);

	if (!error)
	{
		error = nano::flags_config_conflicts (flags, config.node);
	}
	if (!error)
	{
		auto tls_config (std::make_shared<nano::tls_config> ());
		error = nano::read_tls_config_toml (data_path, *tls_config, nlogger);
		if (error)
		{
			std::cerr << error.get_message () << std::endl;
			std::exit (1);
		}
		else
		{
			config.node.websocket_config.tls_config = tls_config;
		}

		boost::asio::io_context io_ctx;
		auto opencl (nano::opencl_work::create (config.opencl_enable, config.opencl, nlogger, config.node.network_params.work));
		nano::work_pool opencl_work (config.node.network_params.network, config.node.work_threads, config.node.pow_sleep_interval, opencl ? [&opencl] (nano::work_version const version_a, nano::root const & root_a, uint64_t difficulty_a, std::atomic<int> & ticket_a) {
			return opencl->generate_work (version_a, root_a, difficulty_a, ticket_a);
		}
																																		  : std::function<boost::optional<uint64_t> (nano::work_version const, nano::root const &, uint64_t, std::atomic<int> &)> (nullptr));
		try
		{
			// This avoids a blank prompt during any node initialization delays
			nlogger.info (nano::log::type::daemon, "Starting up Nano node...");

			// Print info about number of logical cores detected, those are used to decide how many IO, worker and signature checker threads to spawn
			nlogger.info (nano::log::type::daemon, "Hardware concurrency: {} ( configured: {} )", std::thread::hardware_concurrency (), nano::hardware_concurrency ());

			nano::set_file_descriptor_limit (OPEN_FILE_DESCRIPTORS_LIMIT);
			auto const file_descriptor_limit = nano::get_file_descriptor_limit ();
			nlogger.info (nano::log::type::daemon, "File descriptors limit: {}", file_descriptor_limit);
			if (file_descriptor_limit < OPEN_FILE_DESCRIPTORS_LIMIT)
			{
				nlogger.warn (nano::log::type::daemon, "File descriptors limit is lower than the {} recommended. Node was unable to change it.", OPEN_FILE_DESCRIPTORS_LIMIT);
			}

			// for the daemon start up, if the user hasn't specified a port in
			// the config, we must use the default peering port for the network
			//
			if (!config.node.peering_port.has_value ())
			{
				config.node.peering_port = network_params.network.default_node_port;
			}

			auto node (std::make_shared<nano::node> (io_ctx, data_path, config.node, opencl_work, flags));
			if (!node->init_error ())
			{
				auto network_label = node->network_params.network.get_current_network_as_string ();
				std::time_t dateTime = std::time (nullptr);

				nlogger.info (nano::log::type::daemon, "Network: {}", network_label);
				nlogger.info (nano::log::type::daemon, "Version: {}", NANO_VERSION_STRING);
				nlogger.info (nano::log::type::daemon, "Data path: '{}'", node->application_path.string ());
				nlogger.info (nano::log::type::daemon, "Build info: {}", BUILD_INFO);
				nlogger.info (nano::log::type::daemon, "Database backend: {}", node->store.vendor_get ());
				nlogger.info (nano::log::type::daemon, "Start time: {:%c} UTC", fmt::gmtime (dateTime));

				node->start ();

				nano::ipc::ipc_server ipc_server (*node, config.rpc);
				std::unique_ptr<boost::process::child> rpc_process;
				std::unique_ptr<nano::rpc> rpc;
				std::unique_ptr<nano::rpc_handler_interface> rpc_handler;
				if (config.rpc_enable)
				{
					if (!config.rpc.child_process.enable)
					{
						// Launch rpc in-process
						nano::rpc_config rpc_config{ config.node.network_params.network };
						auto error = nano::read_rpc_config_toml (data_path, rpc_config, flags.rpc_config_overrides);
						if (error)
						{
							std::cout << error.get_message () << std::endl;
							std::exit (1);
						}

						rpc_config.tls_config = tls_config;
						rpc_handler = std::make_unique<nano::inprocess_rpc_handler> (*node, ipc_server, config.rpc, [&ipc_server, &workers = node->workers, &io_ctx] () {
							ipc_server.stop ();
							workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::seconds (3), [&io_ctx] () {
								io_ctx.stop ();
							});
						});
						rpc = nano::get_rpc (io_ctx, rpc_config, *rpc_handler);
						rpc->start ();
					}
					else
					{
						// Spawn a child rpc process
						if (!std::filesystem::exists (config.rpc.child_process.rpc_path))
						{
							throw std::runtime_error (std::string ("RPC is configured to spawn a new process however the file cannot be found at: ") + config.rpc.child_process.rpc_path);
						}

						auto network = node->network_params.network.get_current_network_as_string ();

						rpc_process = std::make_unique<boost::process::child> (config.rpc.child_process.rpc_path, "--daemon", "--data_path", data_path.string (), "--network", network);
					}
				}

				debug_assert (!nano::signal_handler_impl);
				nano::signal_handler_impl = [this, &io_ctx] () {
					nlogger.warn (nano::log::type::daemon, "Interrupt signal received, stopping...");

					io_ctx.stop ();
					sig_int_or_term = 1;
				};

				nano::signal_manager sigman;

				// keep trapping Ctrl-C to avoid a second Ctrl-C interrupting tasks started by the first
				sigman.register_signal_handler (SIGINT, &nano::signal_handler, true);

				// sigterm is less likely to come in bunches so only trap it once
				sigman.register_signal_handler (SIGTERM, &nano::signal_handler, false);

				runner = std::make_unique<nano::thread_runner> (io_ctx, node->config.io_threads);
				runner->join ();

				if (sig_int_or_term == 1)
				{
					ipc_server.stop ();
					node->stop ();
					if (rpc)
					{
						rpc->stop ();
					}
				}
				if (rpc_process)
				{
					rpc_process->wait ();
				}
			}
			else
			{
				nlogger.critical (nano::log::type::daemon, "Error initializing node");
			}
		}
		catch (std::runtime_error const & e)
		{
			nlogger.critical (nano::log::type::daemon, "Error while running node: {}", e.what ());
		}
	}
	else
	{
		nlogger.critical (nano::log::type::daemon, "Error deserializing config: {}", error.get_message ());
	}

	nlogger.info (nano::log::type::daemon, "Daemon exiting");
}
