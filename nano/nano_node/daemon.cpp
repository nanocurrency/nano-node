#include <nano/boost/process/child.hpp>
#include <nano/lib/signal_manager.hpp>
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

#include <csignal>
#include <iostream>

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

void nano_daemon::daemon::run (boost::filesystem::path const & data_path, nano::node_flags const & flags)
{
	install_abort_signal_handler ();

	boost::filesystem::create_directories (data_path);
	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (data_path, error_chmod);
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
		config.node.logging.init (data_path);
		nano::logger_mt logger{ config.node.logging.min_time_between_log_output };

		auto tls_config (std::make_shared<nano::tls_config> ());
		error = nano::read_tls_config_toml (data_path, *tls_config, logger);
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
		auto opencl (nano::opencl_work::create (config.opencl_enable, config.opencl, logger, config.node.network_params.work));
		nano::work_pool opencl_work (config.node.network_params.network, config.node.work_threads, config.node.pow_sleep_interval, opencl ? [&opencl] (nano::work_version const version_a, nano::root const & root_a, uint64_t difficulty_a, std::atomic<int> & ticket_a) {
			return opencl->generate_work (version_a, root_a, difficulty_a, ticket_a);
		}
																																		  : std::function<boost::optional<uint64_t> (nano::work_version, nano::root const &, uint64_t, std::atomic<int> &)> (nullptr));
		try
		{
			// This avoid a blank prompt during any node initialization delays
			auto initialization_text = "Starting up Nano node...";
			std::cout << initialization_text << std::endl;
			logger.always_log (initialization_text);

			// Print info about number of logical cores detected, those are used to decide how many IO, worker and signature checker threads to spawn
			logger.always_log (boost::format ("Hardware concurrency: %1% ( configured: %2% )") % std::thread::hardware_concurrency () % nano::hardware_concurrency ());

			nano::set_file_descriptor_limit (OPEN_FILE_DESCRIPTORS_LIMIT);
			auto const file_descriptor_limit = nano::get_file_descriptor_limit ();
			if (file_descriptor_limit < OPEN_FILE_DESCRIPTORS_LIMIT)
			{
				logger.always_log (boost::format ("WARNING: open file descriptors limit is %1%, lower than the %2% recommended. Node was unable to change it.") % file_descriptor_limit % OPEN_FILE_DESCRIPTORS_LIMIT);
			}
			else
			{
				logger.always_log (boost::format ("Open file descriptors limit is %1%") % file_descriptor_limit);
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
				std::unique_ptr<nano::thread_runner> runner;
				auto network_label = node->network_params.network.get_current_network_as_string ();
				std::time_t dateTime = std::time (nullptr);

				std::cout << "Network: " << network_label << ", version: " << NANO_VERSION_STRING << "\n"
						  << "Path: " << node->application_path.string () << "\n"
						  << "Build Info: " << BUILD_INFO << "\n"
						  << "Database backend: " << node->store.vendor_get () << "\n"
						  << "Start time: " << std::put_time (std::gmtime (&dateTime), "%c UTC") << std::endl;

				auto voting (node->wallets.reps ().voting);
				if (voting > 1)
				{
					std::cout << "Voting with more than one representative can limit performance: " << voting << " representatives are configured" << std::endl;
				}
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
						if (!boost::filesystem::exists (config.rpc.child_process.rpc_path))
						{
							throw std::runtime_error (std::string ("RPC is configured to spawn a new process however the file cannot be found at: ") + config.rpc.child_process.rpc_path);
						}

						auto network = node->network_params.network.get_current_network_as_string ();
						rpc_process = std::make_unique<boost::process::child> (config.rpc.child_process.rpc_path, "--daemon", "--data_path", data_path, "--network", network);
					}
				}

				debug_assert (!nano::signal_handler_impl);
				nano::signal_handler_impl = [&io_ctx] () {
					io_ctx.stop ();
					sig_int_or_term = 1;
				};

				nano::signal_manager sigman;

				// keep trapping Ctrl-C to avoid a second Ctrl-C interrupting tasks started by the first
				sigman.register_signal_handler (SIGINT, &nano::signal_handler, true);

				// sigterm is less likely to come in bunches so only trap it once
				sigman.register_signal_handler (SIGTERM, &nano::signal_handler, false);

#ifndef _WIN32
				// on sighup we should reload the bandwidth parameters
				std::function<void (int)> sighup_signal_handler ([&node, &data_path, &flags] (int signum) {
					debug_assert (signum == SIGHUP);
					load_and_set_bandwidth_params (node, data_path, flags);
				});
				sigman.register_signal_handler (SIGHUP, sighup_signal_handler, true);
#endif

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
				std::cerr << "Error initializing node\n";
			}
		}
		catch (std::runtime_error const & e)
		{
			std::cerr << "Error while running node (" << e.what () << ")\n";
		}
	}
	else
	{
		std::cerr << "Error deserializing config: " << error.get_message () << std::endl;
	}
}
