#include <nano/lib/cli.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/signal_manager.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/tlsconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/cli.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/rpc/rpc.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <nano/secure/utility.hpp>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

namespace
{
volatile sig_atomic_t sig_int_or_term = 0;

nano::logger logger{ "rpc_daemon" };

void run (std::filesystem::path const & data_path, std::vector<std::string> const & config_overrides)
{
	logger.info (nano::log::type::daemon_rpc, "Daemon started (RPC)");

	std::filesystem::create_directories (data_path);

	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (data_path, error_chmod);

	std::unique_ptr<nano::thread_runner> runner;

	nano::network_params network_params{ nano::network_constants::active_network };
	nano::rpc_config rpc_config{ network_params.network };
	auto error = nano::read_rpc_config_toml (data_path, rpc_config, config_overrides);
	if (!error)
	{
		auto tls_config (std::make_shared<nano::tls_config> ());
		error = nano::read_tls_config_toml (data_path, *tls_config, logger);
		if (error)
		{
			logger.critical (nano::log::type::daemon, "Error reading RPC TLS config: {}", error.get_message ());
			std::exit (1);
		}
		else
		{
			rpc_config.tls_config = tls_config;
		}

		std::shared_ptr<boost::asio::io_context> io_ctx = std::make_shared<boost::asio::io_context> ();

		nano::signal_manager sigman;
		try
		{
			nano::ipc_rpc_processor ipc_rpc_processor (*io_ctx, rpc_config);
			auto rpc = nano::get_rpc (io_ctx, rpc_config, ipc_rpc_processor);
			rpc->start ();

			debug_assert (!nano::signal_handler_impl);
			nano::signal_handler_impl = [io_ctx_w = std::weak_ptr{ io_ctx }] () {
				logger.warn (nano::log::type::daemon, "Interrupt signal received, stopping...");

				if (auto io_ctx_l = io_ctx_w.lock ())
				{
					io_ctx_l->stop ();
				}
				sig_int_or_term = 1;
			};

			sigman.register_signal_handler (SIGINT, &nano::signal_handler, true);
			sigman.register_signal_handler (SIGTERM, &nano::signal_handler, false);

			runner = std::make_unique<nano::thread_runner> (io_ctx, rpc_config.rpc_process.io_threads);
			runner->join ();

			if (sig_int_or_term == 1)
			{
				rpc->stop ();
			}
		}
		catch (std::runtime_error const & e)
		{
			logger.critical (nano::log::type::daemon, "Error while running RPC: {}", e.what ());
		}
	}
	else
	{
		logger.critical (nano::log::type::daemon, "Error deserializing config: {}", error.get_message ());
	}

	logger.info (nano::log::type::daemon_rpc, "Daemon stopped (RPC)");
}
}

int main (int argc, char * const * argv)
{
	nano::set_umask (); // Make sure the process umask is set before any files are created
	nano::logger::initialize (nano::log_config::cli_default ());

	boost::program_options::options_description description ("Command line options");

	// clang-format off
	description.add_options ()
		("help", "Print out options")
		("config", boost::program_options::value<std::vector<nano::config_key_value_pair>>()->multitoken(), "Pass RPC configuration values. This takes precedence over any values in the configuration file. This option can be repeated multiple times.")
		("daemon", "Start RPC daemon")
		("data_path", boost::program_options::value<std::string> (), "Use the supplied path as the data directory")
		("network", boost::program_options::value<std::string> (), "Use the supplied network (live, test, beta or dev)")
		("version", "Prints out version");
	// clang-format on

	boost::program_options::variables_map vm;
	try
	{
		boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
	}
	catch (boost::program_options::error const & err)
	{
		std::cerr << err.what () << std::endl;
		return 1;
	}
	boost::program_options::notify (vm);

	auto network (vm.find ("network"));
	if (network != vm.end ())
	{
		auto err (nano::network_constants::set_active_network (network->second.as<std::string> ()));
		if (err)
		{
			std::cerr << nano::network_constants::active_network_err_msg << std::endl;
			std::exit (1);
		}
	}

	auto data_path_it = vm.find ("data_path");
	std::filesystem::path data_path ((data_path_it != vm.end ()) ? std::filesystem::path (data_path_it->second.as<std::string> ()) : nano::working_path ());
	if (vm.count ("daemon") > 0)
	{
		std::vector<std::string> config_overrides;
		auto config (vm.find ("config"));
		if (config != vm.end ())
		{
			config_overrides = nano::config_overrides (config->second.as<std::vector<nano::config_key_value_pair>> ());
		}
		run (data_path, config_overrides);
	}
	else if (vm.count ("version"))
	{
		std::cout << "Version " << NANO_VERSION_STRING << "\n"
				  << "Build Info " << BUILD_INFO << std::endl;
	}
	else
	{
		// Issue #3748
		// Regardless how the options were added, output the options in alphabetical order so they are easy to find.
		boost::program_options::options_description sorted_description ("Command line options");
		nano::sort_options_description (description, sorted_description);
		std::cout << sorted_description << std::endl;
	}

	return 1;
}
