#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <iostream>
#include <nano/lib/utility.hpp>
#include <nano/nano_node/daemon.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/ipc.hpp>
#include <nano/node/json_handler.hpp>
#include <nano/node/node.hpp>
#include <nano/node/openclwork.hpp>
#include <nano/node/working.hpp>
#include <nano/rpc/rpc.hpp>

#ifndef BOOST_PROCESS_SUPPORTED
#error BOOST_PROCESS_SUPPORTED must be set, check configuration
#endif

#if BOOST_PROCESS_SUPPORTED
#include <boost/process.hpp>
#endif

void nano_daemon::daemon::run (boost::filesystem::path const & data_path, nano::node_flags const & flags)
{
	boost::filesystem::create_directories (data_path);
	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (data_path, error_chmod);
	std::unique_ptr<nano::thread_runner> runner;
	nano::daemon_config config (data_path);
	auto error = nano::read_and_update_daemon_config (data_path, config);

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
					if (config.rpc.rpc_in_process)
					{
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
#if BOOST_PROCESS_SUPPORTED
						rpc_process = std::make_unique<boost::process::child> (config.rpc.rpc_path, "--daemon");
#else
						auto rpc_exe_command = boost::str (boost::format ("%1% %2%") % config.rpc.rpc_path % "--daemon");
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
