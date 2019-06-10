#include <nano/boost/process.hpp>
#include <nano/lib/utility.hpp>
#include <nano/nano_node/daemon.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/ipc.hpp>
#include <nano/node/json_handler.hpp>
#include <nano/node/node.hpp>
#include <nano/node/openclwork.hpp>
#include <nano/rpc/rpc.hpp>
#include <nano/secure/working.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <csignal>
#include <fstream>
#include <iostream>

// Some builds (mac) fail due to "Boost.Stacktrace requires `_Unwind_Backtrace` function".
#ifndef _WIN32
#ifndef _GNU_SOURCE
#define BEFORE_GNU_SOURCE 0
#define _GNU_SOURCE
#else
#define BEFORE_GNU_SOURCE 1
#endif
#endif
// On Windows this include defines min/max macros, so keep below other includes
// to reduce conflicts with other std functions
#include <boost/stacktrace.hpp>
#ifndef _WIN32
#if !BEFORE_GNU_SOURCE
#undef _GNU_SOURCE
#endif
#endif

namespace
{
#ifdef __linux__
#include <link.h>
// Only on linux. This outputs the load addresses for the executable and shared libraries.
// Useful for debugging should the virtual addresses be randomized.
int output_memory_load_address (dl_phdr_info * info, size_t, void *)
{
	static int counter = 0;
	std::ostringstream ss;
	ss << "nano_node_crash_load_address_dump_" << counter << ".txt";
	std::ofstream file (ss.str ());
	file << "Name: " << info->dlpi_name << "\n";

	for (auto i = 0; i < info->dlpi_phnum; ++i)
	{
		// Only care about the first load address
		if (info->dlpi_phdr[i].p_type == PT_LOAD)
		{
			file << std::hex << (void *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
			break;
		}
	}
	++counter;
	return 0;
}
#endif

void my_abort_signal_handler (int signum)
{
	std::signal (signum, SIG_DFL);
	boost::stacktrace::safe_dump_to ("nano_node_backtrace.dump");

#ifdef __linux__
	dl_iterate_phdr (output_memory_load_address, nullptr);
#endif
}
}

void nano_daemon::daemon::run (boost::filesystem::path const & data_path, nano::node_flags const & flags)
{
	// Override segmentation fault and aborting.
	std::signal (SIGSEGV, &my_abort_signal_handler);
	std::signal (SIGABRT, &my_abort_signal_handler);

	boost::filesystem::create_directories (data_path);
	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (data_path, error_chmod);
	std::unique_ptr<nano::thread_runner> runner;
	nano::daemon_config config (data_path);
	auto error = nano::read_and_update_daemon_config (data_path, config);
	nano::use_memory_pools = config.node.use_memory_pools;

	if (!error)
	{
		config.node.logging.init (data_path);
		nano::logger_mt logger{ config.node.logging.min_time_between_log_output };
		boost::asio::io_context io_ctx;
		auto opencl (nano::opencl_work::create (config.opencl_enable, config.opencl, logger));
		nano::work_pool opencl_work (config.node.work_threads, config.node.pow_sleep_interval, opencl ? [&opencl](nano::uint256_union const & root_a, uint64_t difficulty_a) {
			return opencl->generate_work (root_a, difficulty_a);
		}
		                                                                                              : std::function<boost::optional<uint64_t> (nano::uint256_union const &, uint64_t)> (nullptr));
		nano::alarm alarm (io_ctx);
		nano::node_init init;
		try
		{
			auto node (std::make_shared<nano::node> (init, io_ctx, data_path, alarm, config.node, opencl_work, flags));
			if (!init.error ())
			{
				auto network_label = node->network_params.network.get_current_network_as_string ();
				auto version = (NANO_VERSION_PATCH == 0) ? NANO_MAJOR_MINOR_VERSION : NANO_MAJOR_MINOR_RC_VERSION;
				std::cout << "Network: " << network_label << ", version: " << version << std::endl
				          << "Path: " << node->application_path.string () << std::endl;

				node->start ();
				nano::ipc::ipc_server ipc_server (*node, config.rpc);
#if BOOST_PROCESS_SUPPORTED
				std::unique_ptr<boost::process::child> rpc_process;
#endif
				std::unique_ptr<std::thread> rpc_process_thread;
				std::unique_ptr<nano::rpc> rpc;
				std::unique_ptr<nano::rpc_handler_interface> rpc_handler;
				if (config.rpc_enable)
				{
					if (!config.rpc.child_process.enable)
					{
						// Launch rpc in-process
						nano::rpc_config rpc_config;
						auto error = nano::read_and_update_rpc_config (data_path, rpc_config);
						if (error)
						{
							throw std::runtime_error ("Could not deserialize rpc_config file");
						}
						rpc_handler = std::make_unique<nano::inprocess_rpc_handler> (*node, config.rpc, [&ipc_server]() {
							ipc_server.stop ();
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
#if BOOST_PROCESS_SUPPORTED
						rpc_process = std::make_unique<boost::process::child> (config.rpc.child_process.rpc_path, "--daemon", "--data_path", data_path, "--network", network);
#else
						auto rpc_exe_command = boost::str (boost::format ("%1% --daemon --data_path=%2% --network=%3%") % config.rpc.child_process.rpc_path % data_path % network);
						// clang-format off
						rpc_process_thread = std::make_unique<std::thread> ([rpc_exe_command, &logger = node->logger]() {
							nano::thread_role::set (nano::thread_role::name::rpc_process_container);
							std::system (rpc_exe_command.c_str ());
							logger.always_log ("RPC server has stopped");
						});
						// clang-format on
#endif
					}
				}

				runner = std::make_unique<nano::thread_runner> (io_ctx, node->config.io_threads);
				runner->join ();
#if BOOST_PROCESS_SUPPORTED
				if (rpc_process)
				{
					rpc_process->wait ();
				}
#else
				if (rpc_process_thread)
				{
					rpc_process_thread->join ();
				}
#endif
			}
			else
			{
				std::cerr << "Error initializing node\n";
			}
		}
		catch (const std::runtime_error & e)
		{
			std::cerr << "Error while running node (" << e.what () << ")\n";
		}
	}
	else
	{
		std::cerr << "Error deserializing config: " << error.get_message () << std::endl;
	}
}
