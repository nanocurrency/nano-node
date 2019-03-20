#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <iostream>
#include <nano/lib/utility.hpp>
#include <nano/nano_node/daemon.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/ipc.hpp>
#include <nano/node/node.hpp>
#include <nano/node/rpc.hpp>
#include <nano/node/working.hpp>

nano_daemon::daemon::daemon (boost::filesystem::path const & data_path_a, nano::node_flags const & flags_a) :
data_path (data_path_a),
flags (flags_a)
{
}

nano_daemon::daemon::~daemon ()
{
	stop ();
}

void nano_daemon::daemon::run ()
{
	boost::filesystem::create_directories (data_path);
	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (data_path, error_chmod);
	std::unique_ptr<nano::thread_runner> runner;
	nano::daemon_config config;
	auto error = nano::read_and_update_daemon_config (data_path, config);

	if (!error)
	{
		config.node.logging.init (data_path);
		boost::asio::io_context io_ctx;
		auto opencl (nano::opencl_work::create (config.opencl_enable, config.opencl, config.node.logging));
		nano::work_pool opencl_work (config.node.work_threads, opencl ? [&opencl](nano::uint256_union const & root_a, uint64_t difficulty_a) {
			return opencl->generate_work (root_a, difficulty_a);
		}
		                                                              : std::function<boost::optional<uint64_t> (nano::uint256_union const &, uint64_t)> (nullptr));
		nano::alarm alarm (io_ctx);
		nano::node_init init;
		try
		{
			node = std::make_shared<nano::node> (init, io_ctx, data_path, alarm, config.node, opencl_work, flags);
			if (!init.error ())
			{
				node->start ();
				rpc = get_rpc (io_ctx, *node, config.rpc);
				if (rpc)
				{
					rpc->start (config.rpc_enable);
				}
				ipc = std::make_shared<nano::ipc::ipc_server> (*node, *rpc);
				runner = std::make_unique<nano::thread_runner> (io_ctx, node->config.io_threads);
				runner->join ();
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

void nano_daemon::daemon::stop ()
{
	if (ipc)
	{
		ipc->stop ();
	}

	if (rpc)
	{
		rpc->stop ();
	}

	if (node)
	{
		node->stop ();
	}
}
