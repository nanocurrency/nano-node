#include <nano/lib/stream.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/common.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/make_store.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/manual.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/websocket.hpp>
#include <nano/store/component.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <future>
#include <sstream>

double constexpr nano::node::price_max;
double constexpr nano::node::free_cutoff;

namespace nano
{
extern unsigned char nano_bootstrap_weights_live[];
extern std::size_t nano_bootstrap_weights_live_size;
extern unsigned char nano_bootstrap_weights_beta[];
extern std::size_t nano_bootstrap_weights_beta_size;
}

/*
 * configs
 */

nano::backlog_population::config nano::backlog_population_config (const nano::node_config & config)
{
	nano::backlog_population::config cfg{};
	cfg.enabled = config.frontiers_confirmation != nano::frontiers_confirmation_mode::disabled;
	cfg.frequency = config.backlog_scan_frequency;
	cfg.batch_size = config.backlog_scan_batch_size;
	return cfg;
}

nano::outbound_bandwidth_limiter::config nano::outbound_bandwidth_limiter_config (const nano::node_config & config)
{
	outbound_bandwidth_limiter::config cfg{};
	cfg.standard_limit = config.bandwidth_limit;
	cfg.standard_burst_ratio = config.bandwidth_limit_burst_ratio;
	cfg.bootstrap_limit = config.bootstrap_bandwidth_limit;
	cfg.bootstrap_burst_ratio = config.bootstrap_bandwidth_burst_ratio;
	return cfg;
}

/*
 * node
 */

void nano::node::keepalive (std::string const & address_a, uint16_t port_a)
{
	auto node_l (shared_from_this ());
	network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (address_a, std::to_string (port_a)), [node_l, address_a, port_a] (boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
			{
				auto endpoint (nano::transport::map_endpoint_to_v6 (i->endpoint ()));
				std::weak_ptr<nano::node> node_w (node_l);
				auto channel (node_l->network.find_channel (endpoint));
				if (!channel)
				{
					node_l->network.tcp_channels.start_tcp (endpoint);
				}
				else
				{
					node_l->network.send_keepalive (channel);
				}
			}
		}
		else
		{
			node_l->logger.error (nano::log::type::node, "Error resolving address for keepalive: {}:{} ({})", address_a, port_a, ec.message ());
		}
	});
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (rep_crawler & rep_crawler, std::string const & name)
{
	std::size_t count;
	{
		nano::lock_guard<nano::mutex> guard{ rep_crawler.active_mutex };
		count = rep_crawler.active.size ();
	}

	auto const sizeof_element = sizeof (decltype (rep_crawler.active)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "active", count, sizeof_element }));
	return composite;
}

nano::keypair nano::load_or_create_node_id (std::filesystem::path const & application_path)
{
	auto node_private_key_path = application_path / "node_id_private.key";
	std::ifstream ifs (node_private_key_path.c_str ());
	if (ifs.good ())
	{
		nano::default_logger ().info (nano::log::type::init, "Reading node id from: '{}'", node_private_key_path.string ());

		std::string node_private_key;
		ifs >> node_private_key;
		release_assert (node_private_key.size () == 64);
		nano::keypair kp = nano::keypair (node_private_key);
		return kp;
	}
	else
	{
		// no node_id found, generate new one
		nano::default_logger ().info (nano::log::type::init, "Generating a new node id, saving to: '{}'", node_private_key_path.string ());

		nano::keypair kp;
		std::ofstream ofs (node_private_key_path.c_str (), std::ofstream::out | std::ofstream::trunc);
		ofs << kp.prv.to_string () << std::endl
			<< std::flush;
		ofs.close ();
		release_assert (!ofs.fail ());
		return kp;
	}
}

nano::node::node (boost::asio::io_context & io_ctx_a, uint16_t peering_port_a, std::filesystem::path const & application_path_a, nano::work_pool & work_a, nano::node_flags flags_a, unsigned seq) :
	node (io_ctx_a, application_path_a, nano::node_config (peering_port_a), work_a, flags_a, seq)
{
}

nano::node::node (boost::asio::io_context & io_ctx_a, std::filesystem::path const & application_path_a, nano::node_config const & config_a, nano::work_pool & work_a, nano::node_flags flags_a, unsigned seq) :
	node_id{ load_or_create_node_id (application_path_a) },
	write_database_queue (!flags_a.force_use_write_database_queue && (config_a.rocksdb_config.enable)),
	io_ctx (io_ctx_a),
	node_initialized_latch (1),
	config (config_a),
	network_params{ config.network_params },
	logger{ make_logger_identifier (node_id) },
	stats (config.stats_config),
	workers{ config.background_threads, nano::thread_role::name::worker },
	bootstrap_workers{ config.bootstrap_serving_threads, nano::thread_role::name::bootstrap_worker },
	flags (flags_a),
	work (work_a),
	distributed_work (*this),
	store_impl (nano::make_store (logger, application_path_a, network_params.ledger, flags.read_only, true, config_a.rocksdb_config, config_a.diagnostics_config.txn_tracking, config_a.block_processor_batch_max_time, config_a.lmdb_config, config_a.backup_before_upgrade)),
	store (*store_impl),
	unchecked{ config.max_unchecked_blocks, stats, flags.disable_block_processor_unchecked_deletion },
	wallets_store_impl (std::make_unique<nano::mdb_wallets_store> (application_path_a / "wallets.ldb", config_a.lmdb_config)),
	wallets_store (*wallets_store_impl),
	ledger (store, stats, network_params.ledger, flags_a.generate_cache),
	outbound_limiter{ outbound_bandwidth_limiter_config (config) },
	// empty `config.peering_port` means the user made no port choice at all;
	// otherwise, any value is considered, with `0` having the special meaning of 'let the OS pick a port instead'
	//
	network (*this, config.peering_port.has_value () ? *config.peering_port : 0),
	telemetry{ nano::telemetry::config{ config, flags }, *this, network, observers, network_params, stats },
	bootstrap_initiator (*this),
	bootstrap_server{ store, ledger, network_params.network, stats },
	// BEWARE: `bootstrap` takes `network.port` instead of `config.peering_port` because when the user doesn't specify
	//         a peering port and wants the OS to pick one, the picking happens when `network` gets initialized
	//         (if UDP is active, otherwise it happens when `bootstrap` gets initialized), so then for TCP traffic
	//         we want to tell `bootstrap` to use the already picked port instead of itself picking a different one.
	//         Thus, be very careful if you change the order: if `bootstrap` gets constructed before `network`,
	//         the latter would inherit the port from the former (if TCP is active, otherwise `network` picks first)
	//
	tcp_listener{ std::make_shared<nano::transport::tcp_listener> (network.port, *this, config.tcp_incoming_connections_max) },
	application_path (application_path_a),
	port_mapping (*this),
	rep_crawler (*this),
	vote_processor (active, observers, stats, config, flags, logger, online_reps, rep_crawler, ledger, network_params),
	warmed_up (0),
	block_processor (*this, write_database_queue),
	online_reps (ledger, config),
	history{ config.network_params.voting },
	vote_uniquer{},
	confirmation_height_processor (ledger, write_database_queue, config.conf_height_processor_batch_min_time, logger, node_initialized_latch, flags.confirmation_height_processor_mode),
	vote_cache{ config.vote_cache, stats },
	generator{ config, ledger, wallets, vote_processor, history, network, stats, logger, /* non-final */ false },
	final_generator{ config, ledger, wallets, vote_processor, history, network, stats, logger, /* final */ true },
	active{ *this, confirmation_height_processor, block_processor },
	scheduler_impl{ std::make_unique<nano::scheduler::component> (*this) },
	scheduler{ *scheduler_impl },
	aggregator (config, stats, generator, final_generator, history, ledger, wallets, active),
	wallets (wallets_store.init_error (), *this),
	backlog{ nano::backlog_population_config (config), store, stats },
	ascendboot{ config, block_processor, ledger, network, stats },
	websocket{ config.websocket_config, observers, wallets, ledger, io_ctx, logger },
	epoch_upgrader{ *this, ledger, store, network_params, logger },
	startup_time (std::chrono::steady_clock::now ()),
	node_seq (seq),
	block_broadcast{ network, block_arrival, !flags.disable_block_processor_republishing },
	process_live_dispatcher{ ledger, scheduler.priority, vote_cache, websocket }
{
	logger.debug (nano::log::type::node, "Constructing node...");

	block_broadcast.connect (block_processor);
	process_live_dispatcher.connect (block_processor);

	unchecked.satisfied.add ([this] (nano::unchecked_info const & info) {
		this->block_processor.add (info.block);
	});

	vote_cache.rep_weight_query = [this] (nano::account const & rep) {
		return ledger.weight (rep);
	};

	backlog.activate_callback.add ([this] (store::transaction const & transaction, nano::account const & account, nano::account_info const & account_info, nano::confirmation_height_info const & conf_info) {
		scheduler.priority.activate (account, transaction);
		scheduler.optimistic.activate (account, account_info, conf_info);
	});

	if (!init_error ())
	{
		// Notify election schedulers when AEC frees election slot
		active.vacancy_update = [this] () {
			scheduler.priority.notify ();
			scheduler.hinted.notify ();
			scheduler.optimistic.notify ();
		};

		wallets.observer = [this] (bool active) {
			observers.wallet.notify (active);
		};
		network.channel_observer = [this] (std::shared_ptr<nano::transport::channel> const & channel_a) {
			debug_assert (channel_a != nullptr);
			observers.endpoint.notify (channel_a);
		};
		network.disconnect_observer = [this] () {
			observers.disconnect.notify ();
		};
		if (!config.callback_address.empty ())
		{
			observers.blocks.add ([this] (nano::election_status const & status_a, std::vector<nano::vote_with_weight_info> const & votes_a, nano::account const & account_a, nano::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a) {
				auto block_a (status_a.winner);
				if ((status_a.type == nano::election_status_type::active_confirmed_quorum || status_a.type == nano::election_status_type::active_confirmation_height) && this->block_arrival.recent (block_a->hash ()))
				{
					auto node_l (shared_from_this ());
					background ([node_l, block_a, account_a, amount_a, is_state_send_a, is_state_epoch_a] () {
						boost::property_tree::ptree event;
						event.add ("account", account_a.to_account ());
						event.add ("hash", block_a->hash ().to_string ());
						std::string block_text;
						block_a->serialize_json (block_text);
						event.add ("block", block_text);
						event.add ("amount", amount_a.to_string_dec ());
						if (is_state_send_a)
						{
							event.add ("is_send", is_state_send_a);
							event.add ("subtype", "send");
						}
						// Subtype field
						else if (block_a->type () == nano::block_type::state)
						{
							if (block_a->link ().is_zero ())
							{
								event.add ("subtype", "change");
							}
							else if (is_state_epoch_a)
							{
								debug_assert (amount_a == 0 && node_l->ledger.is_epoch_link (block_a->link ()));
								event.add ("subtype", "epoch");
							}
							else
							{
								event.add ("subtype", "receive");
							}
						}
						std::stringstream ostream;
						boost::property_tree::write_json (ostream, event);
						ostream.flush ();
						auto body (std::make_shared<std::string> (ostream.str ()));
						auto address (node_l->config.callback_address);
						auto port (node_l->config.callback_port);
						auto target (std::make_shared<std::string> (node_l->config.callback_target));
						auto resolver (std::make_shared<boost::asio::ip::tcp::resolver> (node_l->io_ctx));
						resolver->async_resolve (boost::asio::ip::tcp::resolver::query (address, std::to_string (port)), [node_l, address, port, target, body, resolver] (boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
							if (!ec)
							{
								node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
							}
							else
							{
								node_l->logger.error (nano::log::type::rpc_callbacks, "Error resolving callback: {}:{} ({})", address, port, ec.message ());
								node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
							}
						});
					});
				}
			});
		}

		// Add block confirmation type stats regardless of http-callback and websocket subscriptions
		observers.blocks.add ([this] (nano::election_status const & status_a, std::vector<nano::vote_with_weight_info> const & votes_a, nano::account const & account_a, nano::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a) {
			debug_assert (status_a.type != nano::election_status_type::ongoing);
			switch (status_a.type)
			{
				case nano::election_status_type::active_confirmed_quorum:
					this->stats.inc (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out);
					break;
				case nano::election_status_type::active_confirmation_height:
					this->stats.inc (nano::stat::type::confirmation_observer, nano::stat::detail::active_conf_height, nano::stat::dir::out);
					break;
				case nano::election_status_type::inactive_confirmation_height:
					this->stats.inc (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out);
					break;
				default:
					break;
			}
		});
		observers.endpoint.add ([this] (std::shared_ptr<nano::transport::channel> const & channel_a) {
			this->network.send_keepalive_self (channel_a);
		});
		observers.vote.add ([this] (std::shared_ptr<nano::vote> vote_a, std::shared_ptr<nano::transport::channel> const & channel_a, nano::vote_code code_a) {
			debug_assert (code_a != nano::vote_code::invalid);
			auto active_in_rep_crawler (!this->rep_crawler.response (channel_a, vote_a));
			if (active_in_rep_crawler)
			{
				// Representative is defined as online if replying to live votes or rep_crawler queries
				this->online_reps.observe (vote_a->account);
			}
		});

		// Cancelling local work generation
		observers.work_cancel.add ([this] (nano::root const & root_a) {
			this->work.cancel (root_a);
			this->distributed_work.cancel (root_a);
		});

		auto const network_label = network_params.network.get_current_network_as_string ();

		logger.info (nano::log::type::node, "Node starting, version: {}", NANO_VERSION_STRING);
		logger.info (nano::log::type::node, "Build information: {}", BUILD_INFO);
		logger.info (nano::log::type::node, "Active network: {}", network_label);
		logger.info (nano::log::type::node, "Database backend: {}", store.vendor_get ());
		logger.info (nano::log::type::node, "Data path: {}", application_path.string ());
		logger.info (nano::log::type::node, "Work pool threads: {} ({})", work.threads.size (), (work.opencl ? "OpenCL" : "CPU"));
		logger.info (nano::log::type::node, "Work peers: {}", config.work_peers.size ());
		logger.info (nano::log::type::node, "Node ID: {}", node_id.pub.to_node_id ());

		if (!work_generation_enabled ())
		{
			logger.info (nano::log::type::node, "Work generation is disabled");
		}

		logger.info (nano::log::type::node, "Outbound bandwidth limit: {} bytes/s, burst ratio: {}",
		config.bandwidth_limit,
		config.bandwidth_limit_burst_ratio);

		// First do a pass with a read to see if any writing needs doing, this saves needing to open a write lock (and potentially blocking)
		auto is_initialized (false);
		{
			auto const transaction (store.tx_begin_read ());
			is_initialized = (store.account.begin (transaction) != store.account.end ());
		}

		if (!is_initialized && !flags.read_only)
		{
			auto const transaction (store.tx_begin_write ({ tables::accounts, tables::blocks, tables::confirmation_height, tables::frontiers }));
			// Store was empty meaning we just created it, add the genesis block
			store.initialize (transaction, ledger.cache, ledger.constants);
		}

		if (!ledger.block_or_pruned_exists (config.network_params.ledger.genesis->hash ()))
		{
			logger.critical (nano::log::type::node, "Genesis block not found. This commonly indicates a configuration issue, check that the --network or --data_path command line arguments are correct, and also the ledger backend node config option. If using a read-only CLI command a ledger must already exist, start the node with --daemon first.");

			if (network_params.network.is_beta_network ())
			{
				logger.critical (nano::log::type::node, "Beta network may have reset, try clearing database files");
			}

			std::exit (1);
		}

		if (config.enable_voting)
		{
			auto reps = wallets.reps ();
			logger.info (nano::log::type::node, "Voting is enabled, more system resources will be used, local representatives: {}", reps.accounts.size ());
			for (auto const & account : reps.accounts)
			{
				logger.info (nano::log::type::node, "Local representative: {}", account.to_account ());
			}
			if (reps.accounts.size () > 1)
			{
				logger.warn (nano::log::type::node, "Voting with more than one representative can limit performance");
			}
		}

		if ((network_params.network.is_live_network () || network_params.network.is_beta_network ()) && !flags.inactive_node)
		{
			auto const bootstrap_weights = get_bootstrap_weights ();
			ledger.bootstrap_weight_max_blocks = bootstrap_weights.first;

			logger.info (nano::log::type::node, "Initial bootstrap height: {}", ledger.bootstrap_weight_max_blocks);
			logger.info (nano::log::type::node, "Current ledger height:    {}", ledger.cache.block_count.load ());

			// Use bootstrap weights if initial bootstrap is not completed
			const bool use_bootstrap_weight = ledger.cache.block_count < bootstrap_weights.first;
			if (use_bootstrap_weight)
			{
				logger.info (nano::log::type::node, "Using predefined representative weights, since block count is less than bootstrap threshold");

				ledger.bootstrap_weights = bootstrap_weights.second;

				logger.info (nano::log::type::node, "************************************ Bootstrap weights ************************************");

				// Sort the weights
				std::vector<std::pair<nano::account, nano::uint128_t>> sorted_weights (ledger.bootstrap_weights.begin (), ledger.bootstrap_weights.end ());
				std::sort (sorted_weights.begin (), sorted_weights.end (), [] (auto const & entry1, auto const & entry2) {
					return entry1.second > entry2.second;
				});

				for (auto const & rep : sorted_weights)
				{
					logger.info (nano::log::type::node, "Using bootstrap rep weight: {} -> {}",
					rep.first.to_account (),
					nano::uint128_union (rep.second).format_balance (Mxrb_ratio, 0, true));
				}

				logger.info (nano::log::type::node, "************************************ ================= ************************************");
			}
		}

		ledger.pruning = flags.enable_pruning || store.pruned.count (store.tx_begin_read ()) > 0;

		if (ledger.pruning)
		{
			if (config.enable_voting && !flags.inactive_node)
			{
				logger.critical (nano::log::type::node, "Incompatibility detected between config node.enable_voting and existing pruned blocks");
				std::exit (1);
			}
			else if (!flags.enable_pruning && !flags.inactive_node)
			{
				logger.critical (nano::log::type::node, "To start node with existing pruned blocks use launch flag --enable_pruning");
				std::exit (1);
			}
		}
	}
	node_initialized_latch.count_down ();
}

nano::node::~node ()
{
	logger.debug (nano::log::type::node, "Destructing node...");
	stop ();
}

// TODO: Move to a separate class
void nano::node::do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const & address, uint16_t port, std::shared_ptr<std::string> const & target, std::shared_ptr<std::string> const & body, std::shared_ptr<boost::asio::ip::tcp::resolver> const & resolver)
{
	if (i_a != boost::asio::ip::tcp::resolver::iterator{})
	{
		auto node_l (shared_from_this ());
		auto sock (std::make_shared<boost::asio::ip::tcp::socket> (node_l->io_ctx));
		sock->async_connect (i_a->endpoint (), [node_l, target, body, sock, address, port, i_a, resolver] (boost::system::error_code const & ec) mutable {
			if (!ec)
			{
				auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
				req->method (boost::beast::http::verb::post);
				req->target (*target);
				req->version (11);
				req->insert (boost::beast::http::field::host, address);
				req->insert (boost::beast::http::field::content_type, "application/json");
				req->body () = *body;
				req->prepare_payload ();
				boost::beast::http::async_write (*sock, *req, [node_l, sock, address, port, req, i_a, target, body, resolver] (boost::system::error_code const & ec, std::size_t bytes_transferred) mutable {
					if (!ec)
					{
						auto sb (std::make_shared<boost::beast::flat_buffer> ());
						auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
						boost::beast::http::async_read (*sock, *sb, *resp, [node_l, sb, resp, sock, address, port, i_a, target, body, resolver] (boost::system::error_code const & ec, std::size_t bytes_transferred) mutable {
							if (!ec)
							{
								if (boost::beast::http::to_status_class (resp->result ()) == boost::beast::http::status_class::successful)
								{
									node_l->stats.inc (nano::stat::type::http_callback, nano::stat::detail::initiate, nano::stat::dir::out);
								}
								else
								{
									node_l->logger.error (nano::log::type::rpc_callbacks, "Callback to {}:{} failed [status: {}]", address, port, nano::util::to_str (resp->result ()));
									node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
								}
							}
							else
							{
								node_l->logger.error (nano::log::type::rpc_callbacks, "Unable to complete callback: {}:{} ({})", address, port, ec.message ());
								node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
							};
						});
					}
					else
					{
						node_l->logger.error (nano::log::type::rpc_callbacks, "Unable to send callback: {}:{} ({})", address, port, ec.message ());
						node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
					}
				});
			}
			else
			{
				node_l->logger.error (nano::log::type::rpc_callbacks, "Unable to connect to callback address: {}:{} ({})", address, port, ec.message ());
				node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
				++i_a;

				node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
			}
		});
	}
}

bool nano::node::copy_with_compaction (std::filesystem::path const & destination)
{
	return store.copy_db (destination);
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (node & node, std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (collect_container_info (node.work, "work"));
	composite->add_component (collect_container_info (node.ledger, "ledger"));
	composite->add_component (collect_container_info (node.active, "active"));
	composite->add_component (collect_container_info (node.bootstrap_initiator, "bootstrap_initiator"));
	composite->add_component (collect_container_info (*node.tcp_listener, "tcp_listener"));
	composite->add_component (collect_container_info (node.network, "network"));
	composite->add_component (node.telemetry.collect_container_info ("telemetry"));
	composite->add_component (collect_container_info (node.workers, "workers"));
	composite->add_component (collect_container_info (node.observers, "observers"));
	composite->add_component (collect_container_info (node.wallets, "wallets"));
	composite->add_component (collect_container_info (node.vote_processor, "vote_processor"));
	composite->add_component (collect_container_info (node.rep_crawler, "rep_crawler"));
	composite->add_component (collect_container_info (node.block_processor, "block_processor"));
	composite->add_component (collect_container_info (node.block_arrival, "block_arrival"));
	composite->add_component (collect_container_info (node.online_reps, "online_reps"));
	composite->add_component (collect_container_info (node.history, "history"));
	composite->add_component (node.block_uniquer.collect_container_info ("block_uniquer"));
	composite->add_component (node.vote_uniquer.collect_container_info ("vote_uniquer"));
	composite->add_component (collect_container_info (node.confirmation_height_processor, "confirmation_height_processor"));
	composite->add_component (collect_container_info (node.distributed_work, "distributed_work"));
	composite->add_component (collect_container_info (node.aggregator, "request_aggregator"));
	composite->add_component (node.scheduler.collect_container_info ("election_scheduler"));
	composite->add_component (node.vote_cache.collect_container_info ("vote_cache"));
	composite->add_component (collect_container_info (node.generator, "vote_generator"));
	composite->add_component (collect_container_info (node.final_generator, "vote_generator_final"));
	composite->add_component (node.ascendboot.collect_container_info ("bootstrap_ascending"));
	composite->add_component (node.unchecked.collect_container_info ("unchecked"));
	return composite;
}

void nano::node::process_active (std::shared_ptr<nano::block> const & incoming)
{
	block_arrival.add (incoming->hash ());
	block_processor.add (incoming);
}

[[nodiscard]] nano::process_return nano::node::process (store::write_transaction const & transaction, nano::block & block)
{
	return ledger.process (transaction, block);
}

nano::process_return nano::node::process (nano::block & block)
{
	auto const transaction = store.tx_begin_write ({ tables::accounts, tables::blocks, tables::frontiers, tables::pending });
	return process (transaction, block);
}

std::optional<nano::process_return> nano::node::process_local (std::shared_ptr<nano::block> const & block_a)
{
	// Add block hash as recently arrived to trigger automatic rebroadcast and election
	block_arrival.add (block_a->hash ());
	block_broadcast.set_local (block_a);
	return block_processor.add_blocking (block_a);
}

void nano::node::process_local_async (std::shared_ptr<nano::block> const & block_a)
{
	// Add block hash as recently arrived to trigger automatic rebroadcast and election
	block_arrival.add (block_a->hash ());
	// Set current time to trigger automatic rebroadcast and election
	block_processor.add (block_a);
}

void nano::node::start ()
{
	long_inactivity_cleanup ();
	network.start ();
	add_initial_peers ();
	if (!flags.disable_legacy_bootstrap && !flags.disable_ongoing_bootstrap)
	{
		ongoing_bootstrap ();
	}
	if (flags.enable_pruning)
	{
		auto this_l (shared ());
		workers.push_task ([this_l] () {
			this_l->ongoing_ledger_pruning ();
		});
	}
	if (!flags.disable_rep_crawler)
	{
		rep_crawler.start ();
	}
	ongoing_rep_calculation ();
	ongoing_peer_store ();
	ongoing_online_weight_calculation_queue ();

	bool tcp_enabled = false;
	if (config.tcp_incoming_connections_max > 0 && !(flags.disable_bootstrap_listener && flags.disable_tcp_realtime))
	{
		tcp_listener->start ([this] (std::shared_ptr<nano::transport::socket> const & new_connection, boost::system::error_code const & ec_a) {
			if (!ec_a)
			{
				tcp_listener->accept_action (ec_a, new_connection);
			}
			return true;
		});
		tcp_enabled = true;

		if (network.port != tcp_listener->endpoint ().port ())
		{
			network.port = tcp_listener->endpoint ().port ();
		}

		logger.info (nano::log::type::node, "Node peering port: {}", network.port.load ());
	}

	if (!flags.disable_backup)
	{
		backup_wallet ();
	}
	if (!flags.disable_search_pending)
	{
		search_receivable_all ();
	}
	if (!flags.disable_wallet_bootstrap)
	{
		// Delay to start wallet lazy bootstrap
		auto this_l (shared ());
		workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::minutes (1), [this_l] () {
			this_l->bootstrap_wallet ();
		});
	}
	// Start port mapping if external address is not defined and TCP ports are enabled
	if (config.external_address == boost::asio::ip::address_v6::any ().to_string () && tcp_enabled)
	{
		port_mapping.start ();
	}
	wallets.start ();
	active.start ();
	generator.start ();
	final_generator.start ();
	scheduler.start ();
	backlog.start ();
	bootstrap_server.start ();
	if (!flags.disable_ascending_bootstrap)
	{
		ascendboot.start ();
	}
	websocket.start ();
	telemetry.start ();
}

void nano::node::stop ()
{
	// Ensure stop can only be called once
	if (stopped.exchange (true))
	{
		return;
	}

	logger.info (nano::log::type::node, "Node stopping...");

	// Cancels ongoing work generation tasks, which may be blocking other threads
	// No tasks may wait for work generation in I/O threads, or termination signal capturing will be unable to call node::stop()
	distributed_work.stop ();
	backlog.stop ();
	if (!flags.disable_ascending_bootstrap)
	{
		ascendboot.stop ();
	}
	unchecked.stop ();
	block_processor.stop ();
	aggregator.stop ();
	vote_processor.stop ();
	scheduler.stop ();
	active.stop ();
	generator.stop ();
	final_generator.stop ();
	confirmation_height_processor.stop ();
	network.stop ();
	telemetry.stop ();
	websocket.stop ();
	bootstrap_server.stop ();
	bootstrap_initiator.stop ();
	tcp_listener->stop ();
	port_mapping.stop ();
	wallets.stop ();
	stats.stop ();
	epoch_upgrader.stop ();
	workers.stop ();
	// work pool is not stopped on purpose due to testing setup
}

void nano::node::keepalive_preconfigured (std::vector<std::string> const & peers_a)
{
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		// can't use `network.port` here because preconfigured peers are referenced
		// just by their address, so we rely on them listening on the default port
		//
		keepalive (*i, network_params.network.default_node_port);
	}
}

nano::block_hash nano::node::latest (nano::account const & account_a)
{
	auto const transaction (store.tx_begin_read ());
	return ledger.latest (transaction, account_a);
}

nano::uint128_t nano::node::balance (nano::account const & account_a)
{
	auto const transaction (store.tx_begin_read ());
	return ledger.account_balance (transaction, account_a);
}

std::shared_ptr<nano::block> nano::node::block (nano::block_hash const & hash_a)
{
	auto const transaction (store.tx_begin_read ());
	return store.block.get (transaction, hash_a);
}

std::pair<nano::uint128_t, nano::uint128_t> nano::node::balance_pending (nano::account const & account_a, bool only_confirmed_a)
{
	std::pair<nano::uint128_t, nano::uint128_t> result;
	auto const transaction (store.tx_begin_read ());
	result.first = ledger.account_balance (transaction, account_a, only_confirmed_a);
	result.second = ledger.account_receivable (transaction, account_a, only_confirmed_a);
	return result;
}

nano::uint128_t nano::node::weight (nano::account const & account_a)
{
	return ledger.weight (account_a);
}

nano::block_hash nano::node::rep_block (nano::account const & account_a)
{
	auto const transaction (store.tx_begin_read ());
	nano::block_hash result (0);
	auto info = ledger.account_info (transaction, account_a);
	if (info)
	{
		result = ledger.representative (transaction, info->head);
	}
	return result;
}

nano::uint128_t nano::node::minimum_principal_weight ()
{
	return online_reps.trended () / network_params.network.principal_weight_factor;
}

void nano::node::long_inactivity_cleanup ()
{
	bool perform_cleanup = false;
	auto const transaction (store.tx_begin_write ({ tables::online_weight, tables::peers }));
	if (store.online_weight.count (transaction) > 0)
	{
		auto sample (store.online_weight.rbegin (transaction));
		auto n (store.online_weight.end ());
		debug_assert (sample != n);
		auto const one_week_ago = static_cast<std::size_t> ((std::chrono::system_clock::now () - std::chrono::hours (7 * 24)).time_since_epoch ().count ());
		perform_cleanup = sample->first < one_week_ago;
	}
	if (perform_cleanup)
	{
		store.online_weight.clear (transaction);
		store.peer.clear (transaction);
		logger.info (nano::log::type::node, "Removed records of peers and online weight after a long period of inactivity");
	}
}

void nano::node::ongoing_rep_calculation ()
{
	auto now (std::chrono::steady_clock::now ());
	vote_processor.calculate_weights ();
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	workers.add_timed_task (now + std::chrono::minutes (10), [node_w] () {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_rep_calculation ();
		}
	});
}

void nano::node::ongoing_bootstrap ()
{
	auto next_wakeup = network_params.network.bootstrap_interval;
	if (warmed_up < 3)
	{
		// Re-attempt bootstrapping more aggressively on startup
		next_wakeup = std::chrono::seconds (5);
		if (!bootstrap_initiator.in_progress () && !network.empty ())
		{
			++warmed_up;
		}
	}
	if (network_params.network.is_dev_network () && flags.bootstrap_interval != 0)
	{
		// For test purposes allow faster automatic bootstraps
		next_wakeup = std::chrono::seconds (flags.bootstrap_interval);
		++warmed_up;
	}
	// Differential bootstrap with max age (75% of all legacy attempts)
	uint32_t frontiers_age (std::numeric_limits<uint32_t>::max ());
	auto bootstrap_weight_reached (ledger.cache.block_count >= ledger.bootstrap_weight_max_blocks);
	auto previous_bootstrap_count (stats.count (nano::stat::type::bootstrap, nano::stat::detail::initiate, nano::stat::dir::out) + stats.count (nano::stat::type::bootstrap, nano::stat::detail::initiate_legacy_age, nano::stat::dir::out));
	/*
	- Maximum value for 25% of attempts or if block count is below preconfigured value (initial bootstrap not finished)
	- Node shutdown time minus 1 hour for start attempts (warm up)
	- Default age value otherwise (1 day for live network, 1 hour for beta)
	*/
	if (bootstrap_weight_reached)
	{
		if (warmed_up < 3)
		{
			// Find last online weight sample (last active time for node)
			uint64_t last_sample_time (0);
			auto last_record = store.online_weight.rbegin (store.tx_begin_read ());
			if (last_record != store.online_weight.end ())
			{
				last_sample_time = last_record->first;
			}
			uint64_t time_since_last_sample = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count () - static_cast<uint64_t> (last_sample_time / std::pow (10, 9)); // Nanoseconds to seconds
			if (time_since_last_sample + 60 * 60 < std::numeric_limits<uint32_t>::max ())
			{
				frontiers_age = std::max<uint32_t> (static_cast<uint32_t> (time_since_last_sample + 60 * 60), network_params.bootstrap.default_frontiers_age_seconds);
			}
		}
		else if (previous_bootstrap_count % 4 != 0)
		{
			frontiers_age = network_params.bootstrap.default_frontiers_age_seconds;
		}
	}
	// Bootstrap and schedule for next attempt
	bootstrap_initiator.bootstrap (false, boost::str (boost::format ("auto_bootstrap_%1%") % previous_bootstrap_count), frontiers_age);
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	workers.add_timed_task (std::chrono::steady_clock::now () + next_wakeup, [node_w] () {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_bootstrap ();
		}
	});
}

void nano::node::ongoing_peer_store ()
{
	const bool stored{ network.tcp_channels.store_all (true) };
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	workers.add_timed_task (std::chrono::steady_clock::now () + network_params.network.peer_dump_interval, [node_w] () {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_peer_store ();
		}
	});
}

void nano::node::backup_wallet ()
{
	auto transaction (wallets.tx_begin_read ());
	for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
		boost::system::error_code error_chmod;
		auto backup_path (application_path / "backup");

		std::filesystem::create_directories (backup_path);
		nano::set_secure_perm_directory (backup_path, error_chmod);
		i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
	}
	auto this_l (shared ());
	workers.add_timed_task (std::chrono::steady_clock::now () + network_params.node.backup_interval, [this_l] () {
		this_l->backup_wallet ();
	});
}

void nano::node::search_receivable_all ()
{
	// Reload wallets from disk
	wallets.reload ();
	// Search pending
	wallets.search_receivable_all ();
	auto this_l (shared ());
	workers.add_timed_task (std::chrono::steady_clock::now () + network_params.node.search_pending_interval, [this_l] () {
		this_l->search_receivable_all ();
	});
}

void nano::node::bootstrap_wallet ()
{
	std::deque<nano::account> accounts;
	{
		nano::lock_guard<nano::mutex> lock{ wallets.mutex };
		auto const transaction (wallets.tx_begin_read ());
		for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n && accounts.size () < 128; ++i)
		{
			auto & wallet (*i->second);
			nano::lock_guard<std::recursive_mutex> wallet_lock{ wallet.store.mutex };
			for (auto j (wallet.store.begin (transaction)), m (wallet.store.end ()); j != m && accounts.size () < 128; ++j)
			{
				nano::account account (j->first);
				accounts.push_back (account);
			}
		}
	}
	if (!accounts.empty ())
	{
		bootstrap_initiator.bootstrap_wallet (accounts);
	}
}

bool nano::node::collect_ledger_pruning_targets (std::deque<nano::block_hash> & pruning_targets_a, nano::account & last_account_a, uint64_t const batch_read_size_a, uint64_t const max_depth_a, uint64_t const cutoff_time_a)
{
	uint64_t read_operations (0);
	bool finish_transaction (false);
	auto const transaction (store.tx_begin_read ());
	for (auto i (store.confirmation_height.begin (transaction, last_account_a)), n (store.confirmation_height.end ()); i != n && !finish_transaction;)
	{
		++read_operations;
		auto const & account (i->first);
		nano::block_hash hash (i->second.frontier);
		uint64_t depth (0);
		while (!hash.is_zero () && depth < max_depth_a)
		{
			auto block (store.block.get (transaction, hash));
			if (block != nullptr)
			{
				if (block->sideband ().timestamp > cutoff_time_a || depth == 0)
				{
					hash = block->previous ();
				}
				else
				{
					break;
				}
			}
			else
			{
				release_assert (depth != 0);
				hash = 0;
			}
			if (++depth % batch_read_size_a == 0)
			{
				transaction.refresh ();
			}
		}
		if (!hash.is_zero ())
		{
			pruning_targets_a.push_back (hash);
		}
		read_operations += depth;
		if (read_operations >= batch_read_size_a)
		{
			last_account_a = account.number () + 1;
			finish_transaction = true;
		}
		else
		{
			++i;
		}
	}
	return !finish_transaction || last_account_a.is_zero ();
}

void nano::node::ledger_pruning (uint64_t const batch_size_a, bool bootstrap_weight_reached_a)
{
	uint64_t const max_depth (config.max_pruning_depth != 0 ? config.max_pruning_depth : std::numeric_limits<uint64_t>::max ());
	uint64_t const cutoff_time (bootstrap_weight_reached_a ? nano::seconds_since_epoch () - config.max_pruning_age.count () : std::numeric_limits<uint64_t>::max ());
	uint64_t pruned_count (0);
	uint64_t transaction_write_count (0);
	nano::account last_account (1); // 0 Burn account is never opened. So it can be used to break loop
	std::deque<nano::block_hash> pruning_targets;
	bool target_finished (false);
	while ((transaction_write_count != 0 || !target_finished) && !stopped)
	{
		// Search pruning targets
		while (pruning_targets.size () < batch_size_a && !target_finished && !stopped)
		{
			target_finished = collect_ledger_pruning_targets (pruning_targets, last_account, batch_size_a * 2, max_depth, cutoff_time);
		}
		// Pruning write operation
		transaction_write_count = 0;
		if (!pruning_targets.empty () && !stopped)
		{
			auto scoped_write_guard = write_database_queue.wait (nano::writer::pruning);
			auto write_transaction (store.tx_begin_write ({ tables::blocks, tables::pruned }));
			while (!pruning_targets.empty () && transaction_write_count < batch_size_a && !stopped)
			{
				auto const & pruning_hash (pruning_targets.front ());
				auto account_pruned_count (ledger.pruning_action (write_transaction, pruning_hash, batch_size_a));
				transaction_write_count += account_pruned_count;
				pruning_targets.pop_front ();
			}
			pruned_count += transaction_write_count;

			logger.debug (nano::log::type::prunning, "Pruned blocks: {}", pruned_count);
		}
	}

	logger.debug (nano::log::type::prunning, "Total recently pruned block count: {}", pruned_count);
}

void nano::node::ongoing_ledger_pruning ()
{
	auto bootstrap_weight_reached (ledger.cache.block_count >= ledger.bootstrap_weight_max_blocks);
	ledger_pruning (flags.block_processor_batch_size != 0 ? flags.block_processor_batch_size : 2 * 1024, bootstrap_weight_reached);
	auto const ledger_pruning_interval (bootstrap_weight_reached ? config.max_pruning_age : std::min (config.max_pruning_age, std::chrono::seconds (15 * 60)));
	auto this_l (shared ());
	workers.add_timed_task (std::chrono::steady_clock::now () + ledger_pruning_interval, [this_l] () {
		this_l->workers.push_task ([this_l] () {
			this_l->ongoing_ledger_pruning ();
		});
	});
}

int nano::node::price (nano::uint128_t const & balance_a, int amount_a)
{
	debug_assert (balance_a >= amount_a * nano::Gxrb_ratio);
	auto balance_l (balance_a);
	double result (0.0);
	for (auto i (0); i < amount_a; ++i)
	{
		balance_l -= nano::Gxrb_ratio;
		auto balance_scaled ((balance_l / nano::Mxrb_ratio).convert_to<double> ());
		auto units (balance_scaled / 1000.0);
		auto unit_price (((free_cutoff - units) / free_cutoff) * price_max);
		result += std::min (std::max (0.0, unit_price), price_max);
	}
	return static_cast<int> (result * 100.0);
}

uint64_t nano::node::default_difficulty (nano::work_version const version_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case nano::work_version::work_1:
			result = network_params.work.threshold_base (version_a);
			break;
		default:
			debug_assert (false && "Invalid version specified to default_difficulty");
	}
	return result;
}

uint64_t nano::node::default_receive_difficulty (nano::work_version const version_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case nano::work_version::work_1:
			result = network_params.work.epoch_2_receive;
			break;
		default:
			debug_assert (false && "Invalid version specified to default_receive_difficulty");
	}
	return result;
}

uint64_t nano::node::max_work_generate_difficulty (nano::work_version const version_a) const
{
	return nano::difficulty::from_multiplier (config.max_work_generate_multiplier, default_difficulty (version_a));
}

bool nano::node::local_work_generation_enabled () const
{
	return config.work_threads > 0 || work.opencl;
}

bool nano::node::work_generation_enabled () const
{
	return work_generation_enabled (config.work_peers);
}

bool nano::node::work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const & peers_a) const
{
	return !peers_a.empty () || local_work_generation_enabled ();
}

boost::optional<uint64_t> nano::node::work_generate_blocking (nano::block & block_a, uint64_t difficulty_a)
{
	auto opt_work_l (work_generate_blocking (block_a.work_version (), block_a.root (), difficulty_a, block_a.account ()));
	if (opt_work_l.is_initialized ())
	{
		block_a.block_work_set (*opt_work_l);
	}
	return opt_work_l;
}

void nano::node::work_generate (nano::work_version const version_a, nano::root const & root_a, uint64_t difficulty_a, std::function<void (boost::optional<uint64_t>)> callback_a, boost::optional<nano::account> const & account_a, bool secondary_work_peers_a)
{
	auto const & peers_l (secondary_work_peers_a ? config.secondary_work_peers : config.work_peers);
	if (distributed_work.make (version_a, root_a, peers_l, difficulty_a, callback_a, account_a))
	{
		// Error in creating the job (either stopped or work generation is not possible)
		callback_a (boost::none);
	}
}

boost::optional<uint64_t> nano::node::work_generate_blocking (nano::work_version const version_a, nano::root const & root_a, uint64_t difficulty_a, boost::optional<nano::account> const & account_a)
{
	std::promise<boost::optional<uint64_t>> promise;
	work_generate (
	version_a, root_a, difficulty_a, [&promise] (boost::optional<uint64_t> opt_work_a) {
		promise.set_value (opt_work_a);
	},
	account_a);
	return promise.get_future ().get ();
}

boost::optional<uint64_t> nano::node::work_generate_blocking (nano::block & block_a)
{
	debug_assert (network_params.network.is_dev_network ());
	return work_generate_blocking (block_a, default_difficulty (nano::work_version::work_1));
}

boost::optional<uint64_t> nano::node::work_generate_blocking (nano::root const & root_a)
{
	debug_assert (network_params.network.is_dev_network ());
	return work_generate_blocking (root_a, default_difficulty (nano::work_version::work_1));
}

boost::optional<uint64_t> nano::node::work_generate_blocking (nano::root const & root_a, uint64_t difficulty_a)
{
	debug_assert (network_params.network.is_dev_network ());
	return work_generate_blocking (nano::work_version::work_1, root_a, difficulty_a);
}

void nano::node::add_initial_peers ()
{
	if (flags.disable_add_initial_peers)
	{
		logger.warn (nano::log::type::node, "Not adding initial peers because `disable_add_initial_peers` flag is set");
		return;
	}

	auto transaction (store.tx_begin_read ());
	for (auto i (store.peer.begin (transaction)), n (store.peer.end ()); i != n; ++i)
	{
		nano::endpoint endpoint (boost::asio::ip::address_v6 (i->first.address_bytes ()), i->first.port ());
		if (!network.reachout (endpoint, config.allow_local_peers))
		{
			network.tcp_channels.start_tcp (endpoint);
		}
	}
}

void nano::node::start_election (std::shared_ptr<nano::block> const & block)
{
	scheduler.manual.push (block);
}

bool nano::node::block_confirmed (nano::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.block_confirmed (transaction, hash_a);
}

bool nano::node::block_confirmed_or_being_confirmed (nano::store::transaction const & transaction, nano::block_hash const & hash_a)
{
	return confirmation_height_processor.is_processing_block (hash_a) || ledger.block_confirmed (transaction, hash_a);
}

bool nano::node::block_confirmed_or_being_confirmed (nano::block_hash const & hash_a)
{
	return block_confirmed_or_being_confirmed (store.tx_begin_read (), hash_a);
}

void nano::node::ongoing_online_weight_calculation_queue ()
{
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	workers.add_timed_task (std::chrono::steady_clock::now () + (std::chrono::seconds (network_params.node.weight_period)), [node_w] () {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_online_weight_calculation ();
		}
	});
}

bool nano::node::online () const
{
	return rep_crawler.total_weight () > online_reps.delta ();
}

void nano::node::ongoing_online_weight_calculation ()
{
	online_reps.sample ();
	ongoing_online_weight_calculation_queue ();
}

void nano::node::receive_confirmed (store::transaction const & block_transaction_a, nano::block_hash const & hash_a, nano::account const & destination_a)
{
	nano::unique_lock<nano::mutex> lk{ wallets.mutex };
	auto wallets_l = wallets.get_wallets ();
	auto wallet_transaction = wallets.tx_begin_read ();
	lk.unlock ();
	for ([[maybe_unused]] auto const & [id, wallet] : wallets_l)
	{
		if (wallet->store.exists (wallet_transaction, destination_a))
		{
			nano::account representative;
			representative = wallet->store.representative (wallet_transaction);
			auto pending = ledger.pending_info (block_transaction_a, nano::pending_key (destination_a, hash_a));
			if (pending)
			{
				auto amount (pending->amount.number ());
				wallet->receive_async (hash_a, representative, amount, destination_a, [] (std::shared_ptr<nano::block> const &) {});
			}
			else
			{
				if (!ledger.block_or_pruned_exists (block_transaction_a, hash_a))
				{
					logger.warn (nano::log::type::node, "Confirmed block is missing: {}", hash_a.to_string ());
					debug_assert (false, "Confirmed block is missing");
				}
				else
				{
					logger.warn (nano::log::type::node, "Block has already been received: {}", hash_a.to_string ());
				}
			}
		}
	}
}

void nano::node::process_confirmed_data (store::transaction const & transaction_a, std::shared_ptr<nano::block> const & block_a, nano::block_hash const & hash_a, nano::account & account_a, nano::uint128_t & amount_a, bool & is_state_send_a, bool & is_state_epoch_a, nano::account & pending_account_a)
{
	// Faster account calculation
	account_a = block_a->account ();
	if (account_a.is_zero ())
	{
		account_a = block_a->sideband ().account;
	}
	// Faster amount calculation
	auto previous (block_a->previous ());
	bool error (false);
	auto previous_balance (ledger.balance_safe (transaction_a, previous, error));
	auto block_balance = ledger.balance (*block_a);
	if (hash_a != ledger.constants.genesis->account ())
	{
		if (!error)
		{
			amount_a = block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
		}
		else
		{
			amount_a = 0;
		}
	}
	else
	{
		amount_a = nano::dev::constants.genesis_amount;
	}
	if (auto state = dynamic_cast<nano::state_block *> (block_a.get ()))
	{
		if (state->hashables.balance < previous_balance)
		{
			is_state_send_a = true;
		}
		if (amount_a == 0 && network_params.ledger.epochs.is_epoch_link (state->link ()))
		{
			is_state_epoch_a = true;
		}
		pending_account_a = state->hashables.link.as_account ();
	}
	if (auto send = dynamic_cast<nano::send_block *> (block_a.get ()))
	{
		pending_account_a = send->hashables.destination;
	}
}

void nano::node::process_confirmed (nano::election_status const & status_a, uint64_t iteration_a)
{
	auto hash (status_a.winner->hash ());
	decltype (iteration_a) const num_iters = (config.block_processor_batch_max_time / network_params.node.process_confirmed_interval) * 4;
	if (auto block_l = ledger.store.block.get (ledger.store.tx_begin_read (), hash))
	{
		logger.trace (nano::log::type::node, nano::log::detail::process_confirmed, nano::log::arg{ "block", block_l });

		confirmation_height_processor.add (block_l);
	}
	else if (iteration_a < num_iters)
	{
		iteration_a++;
		std::weak_ptr<nano::node> node_w (shared ());
		workers.add_timed_task (std::chrono::steady_clock::now () + network_params.node.process_confirmed_interval, [node_w, status_a, iteration_a] () {
			if (auto node_l = node_w.lock ())
			{
				node_l->process_confirmed (status_a, iteration_a);
			}
		});
	}
	else
	{
		// Do some cleanup due to this block never being processed by confirmation height processor
		active.remove_election_winner_details (hash);
	}
}

std::shared_ptr<nano::node> nano::node::shared ()
{
	return shared_from_this ();
}

int nano::node::store_version ()
{
	auto transaction (store.tx_begin_read ());
	return store.version.get (transaction);
}

bool nano::node::init_error () const
{
	return store.init_error () || wallets_store.init_error ();
}

std::pair<uint64_t, decltype (nano::ledger::bootstrap_weights)> nano::node::get_bootstrap_weights () const
{
	std::unordered_map<nano::account, nano::uint128_t> weights;
	uint8_t const * weight_buffer = network_params.network.is_live_network () ? nano_bootstrap_weights_live : nano_bootstrap_weights_beta;
	std::size_t weight_size = network_params.network.is_live_network () ? nano_bootstrap_weights_live_size : nano_bootstrap_weights_beta_size;
	nano::bufferstream weight_stream ((uint8_t const *)weight_buffer, weight_size);
	nano::uint128_union block_height;
	uint64_t max_blocks = 0;
	if (!nano::try_read (weight_stream, block_height))
	{
		max_blocks = nano::narrow_cast<uint64_t> (block_height.number ());
		while (true)
		{
			nano::account account;
			if (nano::try_read (weight_stream, account.bytes))
			{
				break;
			}
			nano::amount weight;
			if (nano::try_read (weight_stream, weight.bytes))
			{
				break;
			}
			weights[account] = weight.number ();
		}
	}
	return { max_blocks, weights };
}

void nano::node::bootstrap_block (const nano::block_hash & hash)
{
	// If we are running pruning node check if block was not already pruned
	if (!ledger.pruning || !store.pruned.exists (store.tx_begin_read (), hash))
	{
		// We don't have the block, try to bootstrap it
		// TODO: Use ascending bootstraper to bootstrap block hash
	}
}

/** Convenience function to easily return the confirmation height of an account. */
uint64_t nano::node::get_confirmation_height (store::transaction const & transaction_a, nano::account & account_a)
{
	nano::confirmation_height_info info;
	store.confirmation_height.get (transaction_a, account_a, info);
	return info.height;
}

nano::account nano::node::get_node_id () const
{
	return node_id.pub;
};

nano::telemetry_data nano::node::local_telemetry () const
{
	nano::telemetry_data telemetry_data;
	telemetry_data.node_id = node_id.pub;
	telemetry_data.block_count = ledger.cache.block_count;
	telemetry_data.cemented_count = ledger.cache.cemented_count;
	telemetry_data.bandwidth_cap = config.bandwidth_limit;
	telemetry_data.protocol_version = network_params.network.protocol_version;
	telemetry_data.uptime = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - startup_time).count ();
	telemetry_data.unchecked_count = unchecked.count ();
	telemetry_data.genesis_block = network_params.ledger.genesis->hash ();
	telemetry_data.peer_count = nano::narrow_cast<decltype (telemetry_data.peer_count)> (network.size ());
	telemetry_data.account_count = ledger.cache.account_count;
	telemetry_data.major_version = nano::get_major_node_version ();
	telemetry_data.minor_version = nano::get_minor_node_version ();
	telemetry_data.patch_version = nano::get_patch_node_version ();
	telemetry_data.pre_release_version = nano::get_pre_release_node_version ();
	telemetry_data.maker = static_cast<std::underlying_type_t<telemetry_maker>> (ledger.pruning ? telemetry_maker::nf_pruned_node : telemetry_maker::nf_node);
	telemetry_data.timestamp = std::chrono::system_clock::now ();
	telemetry_data.active_difficulty = default_difficulty (nano::work_version::work_1);
	// Make sure this is the final operation!
	telemetry_data.sign (node_id);
	return telemetry_data;
}

std::string nano::node::make_logger_identifier (const nano::keypair & node_id)
{
	// Node identifier consists of first 10 characters of node id
	return node_id.pub.to_node_id ().substr (0, 10);
}

/*
 * node_wrapper
 */

nano::node_wrapper::node_wrapper (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, nano::node_flags const & node_flags_a) :
	network_params{ nano::network_constants::active_network },
	io_context (std::make_shared<boost::asio::io_context> ()),
	work{ network_params.network, 1 }
{
	/*
	 * @warning May throw a filesystem exception
	 */
	std::filesystem::create_directories (path_a);

	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (path_a, error_chmod);

	nano::daemon_config daemon_config{ path_a, network_params };
	auto error = nano::read_node_config_toml (config_path_a, daemon_config, node_flags_a.config_overrides);
	if (error)
	{
		std::cerr << "Error deserializing config file";
		if (!node_flags_a.config_overrides.empty ())
		{
			std::cerr << " or --config option";
		}
		std::cerr << "\n"
				  << error.get_message () << std::endl;
		std::exit (1);
	}

	auto & node_config = daemon_config.node;
	node_config.peering_port = 24000;

	node = std::make_shared<nano::node> (*io_context, path_a, node_config, work, node_flags_a);
}

nano::node_wrapper::~node_wrapper ()
{
	node->stop ();
}

/*
 * inactive_node
 */

nano::inactive_node::inactive_node (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, nano::node_flags const & node_flags_a) :
	node_wrapper (path_a, config_path_a, node_flags_a),
	node (node_wrapper.node)
{
	node_wrapper.node->active.stop ();
}

nano::inactive_node::inactive_node (std::filesystem::path const & path_a, nano::node_flags const & node_flags_a) :
	inactive_node (path_a, path_a, node_flags_a)
{
}

nano::node_flags const & nano::inactive_node_flag_defaults ()
{
	static nano::node_flags node_flags;
	node_flags.inactive_node = true;
	node_flags.read_only = true;
	node_flags.generate_cache.reps = false;
	node_flags.generate_cache.cemented_count = false;
	node_flags.generate_cache.unchecked_count = false;
	node_flags.generate_cache.account_count = false;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_tcp_realtime = true;
	return node_flags;
}
