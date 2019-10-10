#include <nano/boost/process.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/walletconfig.hpp>
#include <nano/nano_wallet/icon.hpp>
#include <nano/node/cli.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/ipc.hpp>
#include <nano/node/json_handler.hpp>
#include <nano/node/node_rpc_config.hpp>
#include <nano/qt/qt.hpp>
#include <nano/rpc/rpc.hpp>
#include <nano/secure/working.hpp>

#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace
{
void show_error (std::string const & message_a)
{
	QMessageBox message (QMessageBox::Critical, "Error starting Nano", message_a.c_str ());
	message.setModal (true);
	message.show ();
	message.exec ();
}
void show_help (std::string const & message_a)
{
	QMessageBox message (QMessageBox::NoIcon, "Help", "see <a href=\"https://docs.nano.org/commands/command-line-interface/#launch-options\">launch options</a> ");
	message.setStyleSheet ("QLabel {min-width: 450px}");
	message.setDetailedText (message_a.c_str ());
	message.show ();
	message.exec ();
}

void update_flags (nano::node_flags & flags_a, boost::program_options::variables_map const & vm)
{
	auto batch_size_it = vm.find ("batch_size");
	if (batch_size_it != vm.end ())
	{
		flags_a.sideband_batch_size = batch_size_it->second.as<size_t> ();
	}
	flags_a.disable_backup = (vm.count ("disable_backup") > 0);
	flags_a.disable_lazy_bootstrap = (vm.count ("disable_lazy_bootstrap") > 0);
	flags_a.disable_legacy_bootstrap = (vm.count ("disable_legacy_bootstrap") > 0);
	flags_a.disable_wallet_bootstrap = (vm.count ("disable_wallet_bootstrap") > 0);
	flags_a.disable_bootstrap_listener = (vm.count ("disable_bootstrap_listener") > 0);
	flags_a.disable_tcp_realtime = (vm.count ("disable_tcp_realtime") > 0);
	flags_a.disable_udp = (vm.count ("disable_udp") > 0);
	if (flags_a.disable_tcp_realtime && flags_a.disable_udp)
	{
		show_error ("Flags --disable_tcp_realtime and --disable_udp cannot be used together");
		std::exit (1);
	}
	flags_a.disable_unchecked_cleanup = (vm.count ("disable_unchecked_cleanup") > 0);
	flags_a.disable_unchecked_drop = (vm.count ("disable_unchecked_drop") > 0);
	flags_a.fast_bootstrap = (vm.count ("fast_bootstrap") > 0);
	if (flags_a.fast_bootstrap)
	{
		flags_a.block_processor_batch_size = 256 * 1024;
		flags_a.block_processor_full_size = 1024 * 1024;
		flags_a.block_processor_verification_size = std::numeric_limits<size_t>::max ();
	}
	auto block_processor_batch_size_it = vm.find ("block_processor_batch_size");
	if (block_processor_batch_size_it != vm.end ())
	{
		flags_a.block_processor_batch_size = block_processor_batch_size_it->second.as<size_t> ();
	}
	auto block_processor_full_size_it = vm.find ("block_processor_full_size");
	if (block_processor_full_size_it != vm.end ())
	{
		flags_a.block_processor_full_size = block_processor_full_size_it->second.as<size_t> ();
	}
	auto block_processor_verification_size_it = vm.find ("block_processor_verification_size");
	if (block_processor_verification_size_it != vm.end ())
	{
		flags_a.block_processor_verification_size = block_processor_verification_size_it->second.as<size_t> ();
	}
}

nano::error read_and_update_wallet_config (nano::wallet_config & config_a, boost::filesystem::path const & data_path_a)
{
	nano::tomlconfig wallet_config_toml;
	auto wallet_path (nano::get_qtwallet_toml_config_path (data_path_a));
	wallet_config_toml.read (nano::get_qtwallet_toml_config_path (data_path_a));
	config_a.serialize_toml (wallet_config_toml);

	// Write wallet config. If missing, the file is created and permissions are set.
	wallet_config_toml.write (wallet_path);
	return wallet_config_toml.get_error ();
}
}

int run_wallet (QApplication & application, int argc, char * const * argv, boost::filesystem::path const & data_path, std::vector<std::string> const & config_overrides, nano::node_flags const & flags)
{
	int result (0);
	nano_qt::eventloop_processor processor;
	boost::system::error_code error_chmod;
	boost::filesystem::create_directories (data_path);
	nano::set_secure_perm_directory (data_path, error_chmod);
	QPixmap pixmap (":/logo.png");
	QSplashScreen * splash = new QSplashScreen (pixmap);
	splash->show ();
	application.processEvents ();
	splash->showMessage (QSplashScreen::tr ("Remember - Back Up Your Wallet Seed"), Qt::AlignBottom | Qt::AlignHCenter, Qt::darkGray);
	application.processEvents ();

	nano::daemon_config config (data_path);
	nano::wallet_config wallet_config;

	auto error = nano::read_node_config_toml (data_path, config, config_overrides);
	if (!error)
	{
		error = read_and_update_wallet_config (wallet_config, data_path);
	}

#if !NANO_ROCKSDB
	if (!error && config.node.rocksdb_config.enable)
	{
		error = nano::error_config::rocksdb_enabled_but_not_supported;
	}
#endif

	if (!error)
	{
		nano::set_use_memory_pools (config.node.use_memory_pools);

		config.node.logging.init (data_path);
		nano::logger_mt logger{ config.node.logging.min_time_between_log_output };

		boost::asio::io_context io_ctx;
		nano::thread_runner runner (io_ctx, config.node.io_threads);

		std::shared_ptr<nano::node> node;
		std::shared_ptr<nano_qt::wallet> gui;
		nano::set_application_icon (application);
		auto opencl (nano::opencl_work::create (config.opencl_enable, config.opencl, logger));
		nano::work_pool work (config.node.work_threads, config.node.pow_sleep_interval, opencl ? [&opencl](nano::root const & root_a, uint64_t difficulty_a, std::atomic<int> &) {
			return opencl->generate_work (root_a, difficulty_a);
		}
		                                                                                       : std::function<boost::optional<uint64_t> (nano::root const &, uint64_t, std::atomic<int> &)> (nullptr));
		nano::alarm alarm (io_ctx);
		node = std::make_shared<nano::node> (io_ctx, data_path, alarm, config.node, work, flags);
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
				if (existing != wallet->store.end ())
				{
					wallet_config.account = existing->first;
				}
				else
				{
					wallet_config.account = wallet->deterministic_insert (transaction);
				}
			}
			assert (wallet->exists (wallet_config.account));
			read_and_update_wallet_config (wallet_config, data_path);
			node->start ();
			nano::ipc::ipc_server ipc (*node, config.rpc);

#if BOOST_PROCESS_SUPPORTED
			std::unique_ptr<boost::process::child> rpc_process;
			std::unique_ptr<boost::process::child> nano_pow_server_process;
#endif

			if (config.pow_server.enable)
			{
				if (!boost::filesystem::exists (config.pow_server.pow_server_path))
				{
					splash->hide ();
					show_error (std::string ("nano_pow_server is configured to start as a child process, however the file cannot be found at: ") + config.pow_server.pow_server_path);
					std::exit (1);
				}

#if BOOST_PROCESS_SUPPORTED
				auto network = node->network_params.network.get_current_network_as_string ();
				nano_pow_server_process = std::make_unique<boost::process::child> (config.pow_server.pow_server_path, "--config_path", data_path / "config-nano-pow-server.toml");
#else
				splash->hide ();
				show_error ("nano_pow_server is configured to start as a child process, but this is not supported on this system. Disable startup and start the server manually.");
				std::exit (1);
#endif
			}

			std::unique_ptr<nano::rpc> rpc;
			std::unique_ptr<nano::rpc_handler_interface> rpc_handler;
			if (config.rpc_enable)
			{
				if (!config.rpc.child_process.enable)
				{
					// Launch rpc in-process
					nano::rpc_config rpc_config;
					auto error = nano::read_rpc_config_toml (data_path, rpc_config);
					if (error)
					{
						show_error (error.get_message ());
					}
					rpc_handler = std::make_unique<nano::inprocess_rpc_handler> (*node, config.rpc);
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

#if BOOST_PROCESS_SUPPORTED
					auto network = node->network_params.network.get_current_network_as_string ();
					rpc_process = std::make_unique<boost::process::child> (config.rpc.child_process.rpc_path, "--daemon", "--data_path", data_path, "--network", network);
#else
					show_error ("rpc_enable is set to true in the config. Set it to false and start the RPC server manually.");
#endif
				}
			}
			QObject::connect (&application, &QApplication::aboutToQuit, [&]() {
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

				if (nano_pow_server_process)
				{
					nano_pow_server_process->terminate ();
				}
#endif
				runner.stop_event_processing ();
			});
			application.postEvent (&processor, new nano_qt::eventloop_event ([&]() {
				gui = std::make_shared<nano_qt::wallet> (application, processor, *node, wallet, wallet_config.account);
				splash->close ();
				gui->start ();
				gui->client_window->show ();
			}));
			result = application.exec ();
			runner.join ();
		}
		else
		{
			splash->hide ();
			show_error ("Error initializing node");
		}
		read_and_update_wallet_config (wallet_config, data_path);
	}
	else
	{
		splash->hide ();
		show_error ("Error deserializing config: " + error.get_message ());
	}
	return result;
}

int main (int argc, char * const * argv)
{
	nano::set_umask ();
	nano::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	try
	{
		QApplication application (argc, const_cast<char **> (argv));
		boost::program_options::options_description description ("Command line options");
		// clang-format off
		description.add_options () 
			("help", "Print out options")
			("config", boost::program_options::value<std::vector<std::string>> ()->multitoken (), "Pass configuration values. This takes precedence over any values in the node configuration file. This option can be repeated multiple times.")
			("disable_backup", "Disable wallet automatic backups")
			("disable_lazy_bootstrap", "Disables lazy bootstrap")
			("disable_legacy_bootstrap", "Disables legacy bootstrap")
			("disable_wallet_bootstrap", "Disables wallet lazy bootstrap")
			("disable_bootstrap_listener", "Disables bootstrap processing for TCP listener (not including realtime network TCP connections)")
			("disable_tcp_realtime", "Disables TCP realtime network")
			("disable_udp", "Disables UDP realtime network")
			("disable_unchecked_cleanup", "Disables periodic cleanup of old records from unchecked table")
			("disable_unchecked_drop", "Disables drop of unchecked table at startup")
			("fast_bootstrap", "Increase bootstrap speed for high end nodes with higher limits")
			("batch_size",boost::program_options::value<std::size_t> (), "Increase sideband batch size, default 512")
			("block_processor_batch_size",boost::program_options::value<std::size_t> (), "Increase block processor transaction batch write size, default 0 (limited by config block_processor_batch_max_time), 256k for fast_bootstrap")
			("block_processor_full_size",boost::program_options::value<std::size_t> (), "Increase block processor allowed blocks queue size before dropping live network packets and holding bootstrap download, default 65536, 1 million for fast_bootstrap")
			("block_processor_verification_size",boost::program_options::value<std::size_t> (), "Increase batch signature verification size in block processor, default 0 (limited by config signature_checker_threads), unlimited for fast_bootstrap");
		nano::add_node_options (description);
		// clang-format on
		boost::program_options::variables_map vm;
		try
		{
			boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
		}
		catch (boost::program_options::error const & err)
		{
			show_error (err.what ());
			return 1;
		}
		boost::program_options::notify (vm);
		int result (0);
		auto network (vm.find ("network"));
		if (network != vm.end ())
		{
			auto err (nano::network_constants::set_active_network (network->second.as<std::string> ()));
			if (err)
			{
				show_error (err.get_message ());
				std::exit (1);
			}
		}

		if (!vm.count ("data_path"))
		{
			std::string error_string;
			if (!nano::migrate_working_path (error_string))
			{
				throw std::runtime_error (error_string);
			}
		}

		std::vector<std::string> config_overrides;
		if (vm.count ("config"))
		{
			config_overrides = vm["config"].as<std::vector<std::string>> ();
		}

		auto ec = nano::handle_node_options (vm);
		if (ec == nano::error_cli::unknown_command)
		{
			if (vm.count ("help") != 0)
			{
				std::ostringstream outstream;
				description.print (outstream);
				std::string helpstring = outstream.str ();
				show_help (helpstring);
				return 1;
			}
			else
			{
				try
				{
					boost::filesystem::path data_path;
					if (vm.count ("data_path"))
					{
						auto name (vm["data_path"].as<std::string> ());
						data_path = boost::filesystem::path (name);
					}
					else
					{
						data_path = nano::working_path ();
					}
					nano::node_flags flags;
					update_flags (flags, vm);
					auto config (vm.find ("config"));
					if (config != vm.end ())
					{
						flags.config_overrides = config->second.as<std::vector<std::string>> ();
					}
					result = run_wallet (application, argc, argv, data_path, config_overrides, flags);
				}
				catch (std::exception const & e)
				{
					show_error (boost::str (boost::format ("Exception while running wallet: %1%") % e.what ()));
				}
				catch (...)
				{
					show_error ("Unknown exception while running wallet");
				}
			}
		}
		return result;
	}
	catch (std::exception const & e)
	{
		show_error (boost::str (boost::format ("Exception while initializing %1%") % e.what ()));
	}
	catch (...)
	{
		show_error (boost::str (boost::format ("Unknown exception while initializing")));
	}
	return 1;
}
