#include <celerix/boost/process/child.hpp>
#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/cli.hpp>
#include <celerix/lib/errors.hpp>
#include <celerix/lib/files.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/rpcconfig.hpp>
#include <celerix/lib/thread_runner.hpp>
#include <celerix/lib/tomlconfig.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/lib/walletconfig.hpp>
#include <celerix/celerix_wallet/icon.hpp>
#include <celerix/node/cli.hpp>
#include <celerix/node/daemonconfig.hpp>
#include <celerix/node/ipc/ipc_server.hpp>
#include <celerix/node/json_handler.hpp>
#include <celerix/node/node_rpc_config.hpp>
#include <celerix/qt/qt.hpp>
#include <celerix/rpc/rpc.hpp>
#include <celerix/secure/working.hpp>

#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace celerix
{
class wallet_daemon final
{
	celerix::logger logger{ "wallet_daemon" };

public:
	void show_error (std::string const & message_a)
	{
		logger.critical (celerix::log::type::daemon, "{}", message_a);

		QMessageBox message (QMessageBox::Critical, "Error starting Celerix", message_a.c_str ());
		message.setModal (true);
		message.show ();
		message.exec ();
	}

	void show_help (std::string const & message_a)
	{
		QMessageBox message (QMessageBox::NoIcon, "Help", "see <a href=\"https://docs.celerix.org/commands/command-line-interface/#launch-options\">launch options</a> ");
		message.setStyleSheet ("QLabel {min-width: 450px}");
		message.setDetailedText (message_a.c_str ());
		message.show ();
		message.exec ();
	}

	celerix::error write_wallet_config (celerix::wallet_config & config_a, std::filesystem::path const & data_path_a)
	{
		celerix::tomlconfig wallet_config_toml;
		auto wallet_path (celerix::get_qtwallet_toml_config_path (data_path_a));
		config_a.serialize_toml (wallet_config_toml);

		// Write wallet config. If missing, the file is created and permissions are set.
		wallet_config_toml.write (wallet_path);
		return wallet_config_toml.get_error ();
	}

	celerix::error read_wallet_config (celerix::wallet_config & config_a, std::filesystem::path const & data_path_a)
	{
		celerix::tomlconfig wallet_config_toml;
		auto wallet_path (celerix::get_qtwallet_toml_config_path (data_path_a));
		if (!std::filesystem::exists (wallet_path))
		{
			write_wallet_config (config_a, data_path_a);
		}
		wallet_config_toml.read (wallet_path);
		config_a.deserialize_toml (wallet_config_toml);
		return wallet_config_toml.get_error ();
	}

	int run_wallet (QApplication & application, int argc, char * const * argv, std::filesystem::path const & data_path, celerix::node_flags const & flags)
	{
		celerix::logger::initialize (celerix::log_config::daemon_default (), data_path, flags.config_overrides);

		logger.info (celerix::log::type::daemon_wallet, "Daemon started (wallet)");

		int result (0);
		celerix_qt::eventloop_processor processor;
		boost::system::error_code error_chmod;
		std::filesystem::create_directories (data_path);
		celerix::set_secure_perm_directory (data_path, error_chmod);
		QPixmap pixmap (":/logo.png");
		auto * splash = new QSplashScreen (pixmap);
		splash->show ();
		QApplication::processEvents ();
		splash->showMessage (QSplashScreen::tr ("Remember - Back Up Your Wallet Seed"), Qt::AlignBottom | Qt::AlignHCenter, Qt::darkGray);
		QApplication::processEvents ();

		celerix::network_params network_params{ celerix::network_constants::active_network };
		celerix::daemon_config config{ data_path, network_params };
		celerix::wallet_config wallet_config;

		auto error = celerix::read_node_config_toml (data_path, config, flags.config_overrides);
		if (!error)
		{
			error = read_wallet_config (wallet_config, data_path);
		}

		if (!error)
		{
			error = celerix::flags_config_conflicts (flags, config.node);
		}

		if (!error)
		{
			celerix::set_use_memory_pools (config.node.use_memory_pools);

			std::shared_ptr<boost::asio::io_context> io_ctx = std::make_shared<boost::asio::io_context> ();

			celerix::thread_runner runner (io_ctx, logger, config.node.io_threads, celerix::thread_role::name::io_daemon);

			std::shared_ptr<celerix::node> node;
			std::shared_ptr<celerix_qt::wallet> gui;
			celerix::set_application_icon (application);
			auto opencl = celerix::opencl_work::create (config.opencl_enable, config.opencl, logger, config.node.network_params.work);
			celerix::opencl_work_func_t opencl_work_func;
			if (opencl)
			{
				opencl_work_func = [&opencl] (celerix::work_version const version_a, celerix::root const & root_a, uint64_t difficulty_a, std::atomic<int> &) {
					return opencl->generate_work (version_a, root_a, difficulty_a);
				};
			}
			celerix::work_pool work{ config.node.network_params.network, config.node.work_threads, config.node.pow_sleep_interval, opencl_work_func };
			node = std::make_shared<celerix::node> (io_ctx, data_path, config.node, work, flags);
			if (!node->init_error ())
			{
				auto wallet (node->wallets.open (wallet_config.wallet));
				if (wallet == nullptr)
				{
					auto existing (node->wallets.items.begin ());
					if (existing != node->wallets.items.end ())
					{
						wallet = existing->second;
						wallet_config.wallet = existing->first;
					}
					else
					{
						wallet = node->wallets.create (wallet_config.wallet);
					}
				}
				if (wallet_config.account.is_zero () || !wallet->exists (wallet_config.account))
				{
					auto transaction (wallet->wallets.tx_begin_write ());
					auto existing (wallet->store.begin (transaction));
					if (existing != wallet->store.end (transaction))
					{
						wallet_config.account = existing->first;
					}
					else
					{
						wallet_config.account = wallet->deterministic_insert (transaction);
					}
				}

				debug_assert (wallet->exists (wallet_config.account));
				write_wallet_config (wallet_config, data_path);
				node->start ();
				celerix::ipc::ipc_server ipc (*node, config.rpc);

				std::unique_ptr<boost::process::child> rpc_process;
				std::shared_ptr<celerix::rpc> rpc;
				std::unique_ptr<celerix::rpc_handler_interface> rpc_handler;
				if (config.rpc_enable)
				{
					if (!config.rpc.child_process.enable)
					{
						// Launch rpc in-process
						celerix::rpc_config rpc_config{ config.node.network_params.network };
						error = celerix::read_rpc_config_toml (data_path, rpc_config, flags.rpc_config_overrides);
						if (error)
						{
							splash->hide ();
							show_error (error.get_message ());
							std::exit (1);
						}
						rpc_handler = std::make_unique<celerix::inprocess_rpc_handler> (*node, ipc, config.rpc);
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
				}
				QObject::connect (&application, &QApplication::aboutToQuit, [&] () {
					ipc.stop ();
					node->stop ();
					if (rpc)
					{
						rpc->stop ();
					}
#if USE_BOOST_PROCESS
					if (rpc_process)
					{
						rpc_process->terminate ();
					}
#endif
					runner.abort ();
				});
				QApplication::postEvent (&processor, new celerix_qt::eventloop_event ([&] () {
					gui = std::make_shared<celerix_qt::wallet> (application, processor, *node, wallet, wallet_config.account);
					splash->close ();
					gui->start ();
					gui->client_window->show ();
				}));
				result = QApplication::exec ();
				runner.join ();
			}
			else
			{
				splash->hide ();
				show_error ("Error initializing node");
			}
			write_wallet_config (wallet_config, data_path);
		}
		else
		{
			splash->hide ();
			show_error ("Error deserializing config: " + error.get_message ());
		}

		logger.info (celerix::log::type::daemon_wallet, "Daemon exiting (wallet)");

		return result;
	}
};
}

int main (int argc, char * const * argv)
{
	celerix::set_umask (); // Make sure the process umask is set before any files are created
	celerix::initialize_file_descriptor_limit ();
	celerix::logger::initialize (celerix::log_config::cli_default ());

	celerix::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;

	QApplication application (argc, const_cast<char **> (argv));

	celerix::wallet_daemon daemon;

	try
	{
		boost::program_options::options_description description ("Command line options");
		// clang-format off
		description.add_options()
			("help", "Print out options")
			("config", boost::program_options::value<std::vector<celerix::config_key_value_pair>>()->multitoken(), "Pass configuration values. This takes precedence over any values in the node configuration file. This option can be repeated multiple times.")
			("rpcconfig", boost::program_options::value<std::vector<celerix::config_key_value_pair>>()->multitoken(), "Pass RPC configuration values. This takes precedence over any values in the RPC configuration file. This option can be repeated multiple times.");
		celerix::add_node_flag_options (description);
		celerix::add_node_options (description);
		// clang-format on
		boost::program_options::variables_map vm;
		try
		{
			boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
		}
		catch (boost::program_options::error const & err)
		{
			daemon.show_error (err.what ());
			return 1;
		}
		boost::program_options::notify (vm);
		int result (0);
		auto network (vm.find ("network"));
		if (network != vm.end ())
		{
			auto err (celerix::network_constants::set_active_network (network->second.as<std::string> ()));
			if (err)
			{
				daemon.show_error ("Invalid network. Valid values are live, test, beta and dev.");
				std::exit (1);
			}
		}

		std::vector<std::string> config_overrides;
		const auto configItr = vm.find ("config");
		if (configItr != vm.cend ())
		{
			config_overrides = celerix::config_overrides (configItr->second.as<std::vector<celerix::config_key_value_pair>> ());
		}

		auto ec = celerix::handle_node_options (vm);
		if (ec == celerix::error_cli::unknown_command)
		{
			if (vm.count ("help") != 0)
			{
				std::ostringstream outstream;
				description.print (outstream);
				std::string helpstring = outstream.str ();
				daemon.show_help (helpstring);
				return 1;
			}
			else
			{
				try
				{
					std::filesystem::path data_path;
					if (vm.count ("data_path"))
					{
						auto name (vm["data_path"].as<std::string> ());
						data_path = std::filesystem::path (name);
					}
					else
					{
						data_path = celerix::working_path ();
					}
					celerix::node_flags flags;
					auto flags_ec = celerix::update_flags (flags, vm);
					if (flags_ec)
					{
						throw std::runtime_error (flags_ec.message ());
					}
					result = daemon.run_wallet (application, argc, argv, data_path, flags);
				}
				catch (std::exception const & e)
				{
					daemon.show_error (boost::str (boost::format ("Exception while running wallet: %1%") % e.what ()));
				}
				catch (...)
				{
					daemon.show_error ("Unknown exception while running wallet");
				}
			}
		}
		return result;
	}
	catch (std::exception const & e)
	{
		daemon.show_error (boost::str (boost::format ("Exception while initializing %1%") % e.what ()));
	}
	catch (...)
	{
		daemon.show_error (boost::str (boost::format ("Unknown exception while initializing")));
	}
	return 1;
}
