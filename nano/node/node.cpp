#include <nano/lib/blocks.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/backlog_population.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/common.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/election_status.hpp>
#include <nano/node/local_block_broadcaster.hpp>
#include <nano/node/local_vote_history.hpp>
#include <nano/node/make_store.hpp>
#include <nano/node/message_processor.hpp>
#include <nano/node/monitor.hpp>
#include <nano/node/node.hpp>
#include <nano/node/peer_history.hpp>
#include <nano/node/portmapping.hpp>
#include <nano/node/request_aggregator.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/manual.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/vote_generator.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/node/websocket.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
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
 * node
 */

nano::node::node (std::shared_ptr<boost::asio::io_context> io_ctx_a, uint16_t peering_port_a, std::filesystem::path const & application_path_a, nano::work_pool & work_a, nano::node_flags flags_a, unsigned seq) :
	node (io_ctx_a, application_path_a, nano::node_config (peering_port_a), work_a, flags_a, seq)
{
}

nano::node::node (std::shared_ptr<boost::asio::io_context> io_ctx_a, std::filesystem::path const & application_path_a, nano::node_config const & config_a, nano::work_pool & work_a, nano::node_flags flags_a, unsigned seq) :
	node_id{ load_or_create_node_id (application_path_a) },
	config{ config_a },
	io_ctx_shared{ std::make_shared<boost::asio::io_context> () },
	io_ctx{ *io_ctx_shared },
	logger{ make_logger_identifier (node_id) },
	runner_impl{ std::make_unique<nano::thread_runner> (io_ctx_shared, logger, config.io_threads) },
	runner{ *runner_impl },
	node_initialized_latch (1),
	network_params{ config.network_params },
	stats{ logger, config.stats_config },
	workers{ config.background_threads, nano::thread_role::name::worker },
	bootstrap_workers{ config.bootstrap_serving_threads, nano::thread_role::name::bootstrap_worker },
	wallet_workers{ 1, nano::thread_role::name::wallet_worker },
	election_workers{ 1, nano::thread_role::name::election_worker },
	flags (flags_a),
	work (work_a),
	distributed_work (*this),
	store_impl (nano::make_store (logger, application_path_a, network_params.ledger, flags.read_only, true, config_a.rocksdb_config, config_a.diagnostics_config.txn_tracking, config_a.block_processor_batch_max_time, config_a.lmdb_config, config_a.backup_before_upgrade, flags.force_use_write_queue)),
	store (*store_impl),
	unchecked{ config.max_unchecked_blocks, stats, flags.disable_block_processor_unchecked_deletion },
	wallets_store_impl (std::make_unique<nano::mdb_wallets_store> (application_path_a / "wallets.ldb", config_a.lmdb_config)),
	wallets_store (*wallets_store_impl),
	ledger_impl{ std::make_unique<nano::ledger> (store, stats, network_params.ledger, flags_a.generate_cache, config_a.representative_vote_weight_minimum.number ()) },
	ledger{ *ledger_impl },
	outbound_limiter_impl{ std::make_unique<nano::bandwidth_limiter> (config) },
	outbound_limiter{ *outbound_limiter_impl },
	message_processor_impl{ std::make_unique<nano::message_processor> (config.message_processor, *this) },
	message_processor{ *message_processor_impl },
	// empty `config.peering_port` means the user made no port choice at all;
	// otherwise, any value is considered, with `0` having the special meaning of 'let the OS pick a port instead'
	//
	network (*this, config.peering_port.has_value () ? *config.peering_port : 0),
	telemetry_impl{ std::make_unique<nano::telemetry> (flags, *this, network, observers, network_params, stats) },
	telemetry{ *telemetry_impl },
	bootstrap_initiator (*this),
	bootstrap_server{ config.bootstrap_server, store, ledger, network_params.network, stats },
	// BEWARE: `bootstrap` takes `network.port` instead of `config.peering_port` because when the user doesn't specify
	//         a peering port and wants the OS to pick one, the picking happens when `network` gets initialized
	//         (if UDP is active, otherwise it happens when `bootstrap` gets initialized), so then for TCP traffic
	//         we want to tell `bootstrap` to use the already picked port instead of itself picking a different one.
	//         Thus, be very careful if you change the order: if `bootstrap` gets constructed before `network`,
	//         the latter would inherit the port from the former (if TCP is active, otherwise `network` picks first)
	//
	tcp_listener_impl{ std::make_unique<nano::transport::tcp_listener> (network.port, config.tcp, *this) },
	tcp_listener{ *tcp_listener_impl },
	application_path (application_path_a),
	port_mapping_impl{ std::make_unique<nano::port_mapping> (*this) },
	port_mapping{ *port_mapping_impl },
	block_processor (*this),
	confirming_set_impl{ std::make_unique<nano::confirming_set> (config.confirming_set, ledger, stats) },
	confirming_set{ *confirming_set_impl },
	active_impl{ std::make_unique<nano::active_elections> (*this, confirming_set, block_processor) },
	active{ *active_impl },
	rep_crawler (config.rep_crawler, *this),
	rep_tiers{ ledger, network_params, online_reps, stats, logger },
	warmed_up (0),
	online_reps (ledger, config),
	history_impl{ std::make_unique<nano::local_vote_history> (config.network_params.voting) },
	history{ *history_impl },
	vote_uniquer{},
	vote_cache{ config.vote_cache, stats },
	vote_router_impl{ std::make_unique<nano::vote_router> (vote_cache, active.recently_confirmed) },
	vote_router{ *vote_router_impl },
	vote_processor_impl{ std::make_unique<nano::vote_processor> (config.vote_processor, vote_router, observers, stats, flags, logger, online_reps, rep_crawler, ledger, network_params, rep_tiers) },
	vote_processor{ *vote_processor_impl },
	vote_cache_processor_impl{ std::make_unique<nano::vote_cache_processor> (config.vote_processor, vote_router, vote_cache, stats, logger) },
	vote_cache_processor{ *vote_cache_processor_impl },
	generator_impl{ std::make_unique<nano::vote_generator> (config, *this, ledger, wallets, vote_processor, history, network, stats, logger, /* non-final */ false) },
	generator{ *generator_impl },
	final_generator_impl{ std::make_unique<nano::vote_generator> (config, *this, ledger, wallets, vote_processor, history, network, stats, logger, /* final */ true) },
	final_generator{ *final_generator_impl },
	scheduler_impl{ std::make_unique<nano::scheduler::component> (*this) },
	scheduler{ *scheduler_impl },
	aggregator_impl{ std::make_unique<nano::request_aggregator> (config.request_aggregator, *this, stats, generator, final_generator, history, ledger, wallets, vote_router) },
	aggregator{ *aggregator_impl },
	wallets (wallets_store.init_error (), *this),
	backlog_impl{ std::make_unique<nano::backlog_population> (config.backlog_population, scheduler, ledger, stats) },
	backlog{ *backlog_impl },
	ascendboot_impl{ std::make_unique<nano::bootstrap_ascending::service> (config, block_processor, ledger, network, stats, logger) },
	ascendboot{ *ascendboot_impl },
	websocket{ config.websocket_config, observers, wallets, ledger, io_ctx, logger },
	epoch_upgrader{ *this, ledger, store, network_params, logger },
	local_block_broadcaster_impl{ std::make_unique<nano::local_block_broadcaster> (config.local_block_broadcaster, *this, block_processor, network, confirming_set, stats, logger, !flags.disable_block_processor_republishing) },
	local_block_broadcaster{ *local_block_broadcaster_impl },
	process_live_dispatcher{ ledger, scheduler.priority, vote_cache, websocket },
	peer_history_impl{ std::make_unique<nano::peer_history> (config.peer_history, store, network, logger, stats) },
	peer_history{ *peer_history_impl },
	monitor_impl{ std::make_unique<nano::monitor> (config.monitor, *this) },
	monitor{ *monitor_impl },
	startup_time (std::chrono::steady_clock::now ()),
	node_seq (seq)
{
	logger.debug (nano::log::type::node, "Constructing node...");

	process_live_dispatcher.connect (block_processor);

	unchecked.satisfied.add ([this] (nano::unchecked_info const & info) {
		block_processor.add (info.block, nano::block_source::unchecked);
	});

	vote_cache.rep_weight_query = [this] (nano::account const & rep) {
		return ledger.weight (rep);
	};

	vote_router.vote_processed.add ([this] (std::shared_ptr<nano::vote> const & vote, nano::vote_source source, std::unordered_map<nano::block_hash, nano::vote_code> const & results) {
		if (source != nano::vote_source::cache)
		{
			vote_cache.insert (vote, results);
		}
	});

	// Republish vote if it is new and the node does not host a principal representative (or close to)
	vote_router.vote_processed.add ([this] (std::shared_ptr<nano::vote> const & vote, nano::vote_source source, std::unordered_map<nano::block_hash, nano::vote_code> const & results) {
		bool processed = std::any_of (results.begin (), results.end (), [] (auto const & result) {
			return result.second == nano::vote_code::vote;
		});
		if (processed)
		{
			auto const reps = wallets.reps ();
			if (!reps.have_half_rep () && !reps.exists (vote->account))
			{
				network.flood_vote (vote, 0.5f, /* rebroadcasted */ true);
			}
		}
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
				if ((status_a.type == nano::election_status_type::active_confirmed_quorum || status_a.type == nano::election_status_type::active_confirmation_height))
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
							if (block_a->is_change ())
							{
								event.add ("subtype", "change");
							}
							else if (is_state_epoch_a)
							{
								debug_assert (amount_a == 0 && node_l->ledger.is_epoch_link (block_a->link_field ().value ()));
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

		observers.endpoint.add ([this] (std::shared_ptr<nano::transport::channel> const & channel_a) {
			this->network.send_keepalive_self (channel_a);
		});

		observers.vote.add ([this] (std::shared_ptr<nano::vote> vote, std::shared_ptr<nano::transport::channel> const & channel, nano::vote_source source, nano::vote_code code) {
			debug_assert (vote != nullptr);
			debug_assert (code != nano::vote_code::invalid);
			if (channel == nullptr)
			{
				return; // Channel expired when waiting for vote to be processed
			}
			// Ignore republished votes
			if (source == nano::vote_source::live)
			{
				bool active_in_rep_crawler = rep_crawler.process (vote, channel);
				if (active_in_rep_crawler)
				{
					// Representative is defined as online if replying to live votes or rep_crawler queries
					online_reps.observe (vote->account);
				}
			}
		});

		// Cancelling local work generation
		observers.work_cancel.add ([this] (nano::root const & root_a) {
			this->work.cancel (root_a);
			this->distributed_work.cancel (root_a);
		});

		auto const network_label = network_params.network.get_current_network_as_string ();

		logger.info (nano::log::type::node, "Version: {}", NANO_VERSION_STRING);
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
			auto const transaction (store.tx_begin_write ({ tables::accounts, tables::blocks, tables::confirmation_height, tables::rep_weights }));
			// Store was empty meaning we just created it, add the genesis block
			store.initialize (transaction, ledger.cache, ledger.constants);
		}

		if (!block_or_pruned_exists (config.network_params.ledger.genesis->hash ()))
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
			logger.info (nano::log::type::node, "Current ledger height:    {}", ledger.block_count ());

			// Use bootstrap weights if initial bootstrap is not completed
			const bool use_bootstrap_weight = ledger.block_count () < bootstrap_weights.first;
			if (use_bootstrap_weight)
			{
				logger.info (nano::log::type::node, "Using predefined representative weights, since block count is less than bootstrap threshold");

				ledger.bootstrap_weights = bootstrap_weights.second;

				logger.info (nano::log::type::node, "******************************************** Bootstrap weights ********************************************");

				// Sort the weights
				std::vector<std::pair<nano::account, nano::uint128_t>> sorted_weights (ledger.bootstrap_weights.begin (), ledger.bootstrap_weights.end ());
				std::sort (sorted_weights.begin (), sorted_weights.end (), [] (auto const & entry1, auto const & entry2) {
					return entry1.second > entry2.second;
				});

				for (auto const & rep : sorted_weights)
				{
					logger.info (nano::log::type::node, "Using bootstrap rep weight: {} -> {}",
					rep.first.to_account (),
					nano::uint128_union (rep.second).format_balance (nano_ratio, 0, true));
				}

				logger.info (nano::log::type::node, "******************************************** ================= ********************************************");
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
		confirming_set.cemented_observers.add ([this] (auto const & block) {
			// TODO: Is it neccessary to call this for all blocks?
			if (block->is_send ())
			{
				wallet_workers.push_task ([this, hash = block->hash (), destination = block->destination ()] () {
					wallets.receive_confirmed (hash, destination);
				});
			}
		});
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
				node_l->logger.error (nano::log::type::rpc_callbacks, "Unable to connect to callback address({}): {}:{} ({})", address, i_a->endpoint ().address ().to_string (), port, ec.message ());
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

void nano::node::keepalive (std::string const & address_a, uint16_t port_a)
{
	auto node_l (shared_from_this ());
	network.resolver.async_resolve (boost::asio::ip::tcp::resolver::query (address_a, std::to_string (port_a)), [node_l, address_a, port_a] (boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::tcp::resolver::iterator{}); i != n; ++i)
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

std::unique_ptr<nano::container_info_component> nano::collect_container_info (node & node, std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (collect_container_info (node.work, "work"));
	composite->add_component (node.ledger.collect_container_info ("ledger"));
	composite->add_component (collect_container_info (node.active, "active"));
	composite->add_component (collect_container_info (node.bootstrap_initiator, "bootstrap_initiator"));
	composite->add_component (node.tcp_listener.collect_container_info ("tcp_listener"));
	composite->add_component (collect_container_info (node.network, "network"));
	composite->add_component (node.telemetry.collect_container_info ("telemetry"));
	composite->add_component (node.workers.collect_container_info ("workers"));
	composite->add_component (node.bootstrap_workers.collect_container_info ("bootstrap_workers"));
	composite->add_component (node.wallet_workers.collect_container_info ("wallet_workers"));
	composite->add_component (node.election_workers.collect_container_info ("election_workers"));
	composite->add_component (collect_container_info (node.observers, "observers"));
	composite->add_component (collect_container_info (node.wallets, "wallets"));
	composite->add_component (node.vote_processor.collect_container_info ("vote_processor"));
	composite->add_component (node.vote_cache_processor.collect_container_info ("vote_cache_processor"));
	composite->add_component (node.rep_crawler.collect_container_info ("rep_crawler"));
	composite->add_component (node.block_processor.collect_container_info ("block_processor"));
	composite->add_component (collect_container_info (node.online_reps, "online_reps"));
	composite->add_component (node.history.collect_container_info ("history"));
	composite->add_component (node.block_uniquer.collect_container_info ("block_uniquer"));
	composite->add_component (node.vote_uniquer.collect_container_info ("vote_uniquer"));
	composite->add_component (node.confirming_set.collect_container_info ("confirming_set"));
	composite->add_component (collect_container_info (node.distributed_work, "distributed_work"));
	composite->add_component (node.aggregator.collect_container_info ("request_aggregator"));
	composite->add_component (node.scheduler.collect_container_info ("election_scheduler"));
	composite->add_component (node.vote_cache.collect_container_info ("vote_cache"));
	composite->add_component (node.vote_router.collect_container_info ("vote_router"));
	composite->add_component (node.generator.collect_container_info ("vote_generator"));
	composite->add_component (node.final_generator.collect_container_info ("vote_generator_final"));
	composite->add_component (node.ascendboot.collect_container_info ("bootstrap_ascending"));
	composite->add_component (node.unchecked.collect_container_info ("unchecked"));
	composite->add_component (node.local_block_broadcaster.collect_container_info ("local_block_broadcaster"));
	composite->add_component (node.rep_tiers.collect_container_info ("rep_tiers"));
	composite->add_component (node.message_processor.collect_container_info ("message_processor"));
	return composite;
}

void nano::node::process_active (std::shared_ptr<nano::block> const & incoming)
{
	block_processor.add (incoming);
}

[[nodiscard]] nano::block_status nano::node::process (secure::write_transaction const & transaction, std::shared_ptr<nano::block> block)
{
	auto status = ledger.process (transaction, block);
	logger.debug (nano::log::type::node, "Directly processed block: {} (status: {})", block->hash ().to_string (), to_string (status));
	return status;
}

nano::block_status nano::node::process (std::shared_ptr<nano::block> block)
{
	auto const transaction = ledger.tx_begin_write ({ tables::accounts, tables::blocks, tables::pending, tables::rep_weights }, nano::store::writer::node);
	return process (transaction, block);
}

std::optional<nano::block_status> nano::node::process_local (std::shared_ptr<nano::block> const & block_a)
{
	return block_processor.add_blocking (block_a, nano::block_source::local);
}

void nano::node::process_local_async (std::shared_ptr<nano::block> const & block_a)
{
	block_processor.add (block_a, nano::block_source::local);
}

void nano::node::start ()
{
	long_inactivity_cleanup ();

	network.start ();
	message_processor.start ();

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

	ongoing_online_weight_calculation_queue ();

	bool tcp_enabled = false;
	if (config.tcp_incoming_connections_max > 0 && !(flags.disable_bootstrap_listener && flags.disable_tcp_realtime))
	{
		tcp_listener.start ();
		tcp_enabled = true;

		if (network.port != tcp_listener.endpoint ().port ())
		{
			network.port = tcp_listener.endpoint ().port ();
		}

		logger.info (nano::log::type::node, "Peering port: {}", network.port.load ());
	}
	else
	{
		logger.warn (nano::log::type::node, "Peering is disabled");
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
	unchecked.start ();
	wallets.start ();
	rep_tiers.start ();
	vote_processor.start ();
	vote_cache_processor.start ();
	block_processor.start ();
	active.start ();
	generator.start ();
	final_generator.start ();
	confirming_set.start ();
	scheduler.start ();
	aggregator.start ();
	backlog.start ();
	bootstrap_server.start ();
	if (!flags.disable_ascending_bootstrap)
	{
		ascendboot.start ();
	}
	websocket.start ();
	telemetry.start ();
	stats.start ();
	local_block_broadcaster.start ();
	peer_history.start ();
	vote_router.start ();
	monitor.start ();

	add_initial_peers ();
}

void nano::node::stop ()
{
	// Ensure stop can only be called once
	if (stopped.exchange (true))
	{
		return;
	}

	logger.info (nano::log::type::node, "Node stopping...");

	tcp_listener.stop ();
	bootstrap_workers.stop ();
	wallet_workers.stop ();
	election_workers.stop ();
	vote_router.stop ();
	peer_history.stop ();
	// Cancels ongoing work generation tasks, which may be blocking other threads
	// No tasks may wait for work generation in I/O threads, or termination signal capturing will be unable to call node::stop()
	distributed_work.stop ();
	backlog.stop ();
	ascendboot.stop ();
	rep_crawler.stop ();
	unchecked.stop ();
	block_processor.stop ();
	aggregator.stop ();
	vote_cache_processor.stop ();
	vote_processor.stop ();
	rep_tiers.stop ();
	scheduler.stop ();
	active.stop ();
	generator.stop ();
	final_generator.stop ();
	confirming_set.stop ();
	telemetry.stop ();
	websocket.stop ();
	bootstrap_server.stop ();
	bootstrap_initiator.stop ();
	port_mapping.stop ();
	wallets.stop ();
	stats.stop ();
	epoch_upgrader.stop ();
	workers.stop ();
	local_block_broadcaster.stop ();
	message_processor.stop ();
	network.stop (); // Stop network last to avoid killing in-use sockets
	monitor.stop ();

	// work pool is not stopped on purpose due to testing setup

	// Stop the IO runner last
	runner.join ();
	debug_assert (io_ctx_shared.use_count () == 1); // Node should be the last user of the io_context
}

void nano::node::keepalive_preconfigured ()
{
	for (auto const & peer : config.preconfigured_peers)
	{
		// can't use `network.port` here because preconfigured peers are referenced
		// just by their address, so we rely on them listening on the default port
		//
		keepalive (peer, network_params.network.default_node_port);
	}
}

nano::block_hash nano::node::latest (nano::account const & account_a)
{
	return ledger.any.account_head (ledger.tx_begin_read (), account_a);
}

nano::uint128_t nano::node::balance (nano::account const & account_a)
{
	return ledger.any.account_balance (ledger.tx_begin_read (), account_a).value_or (0).number ();
}

std::shared_ptr<nano::block> nano::node::block (nano::block_hash const & hash_a)
{
	return ledger.any.block_get (ledger.tx_begin_read (), hash_a);
}

bool nano::node::block_or_pruned_exists (nano::block_hash const & hash_a) const
{
	return ledger.any.block_exists_or_pruned (ledger.tx_begin_read (), hash_a);
}

std::pair<nano::uint128_t, nano::uint128_t> nano::node::balance_pending (nano::account const & account_a, bool only_confirmed_a)
{
	std::pair<nano::uint128_t, nano::uint128_t> result;
	auto const transaction = ledger.tx_begin_read ();
	result.first = only_confirmed_a ? ledger.confirmed.account_balance (transaction, account_a).value_or (0).number () : ledger.any.account_balance (transaction, account_a).value_or (0).number ();
	result.second = ledger.account_receivable (transaction, account_a, only_confirmed_a);
	return result;
}

nano::uint128_t nano::node::weight (nano::account const & account_a)
{
	auto txn = ledger.tx_begin_read ();
	return ledger.weight_exact (txn, account_a);
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
	auto bootstrap_weight_reached (ledger.block_count () >= ledger.bootstrap_weight_max_blocks);
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
			{
				auto transaction = store.tx_begin_read ();
				auto last_record = store.online_weight.rbegin (transaction);
				if (last_record != store.online_weight.end ())
				{
					last_sample_time = last_record->first;
				}
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
	auto transaction = ledger.tx_begin_read ();
	for (auto i (store.confirmation_height.begin (transaction, last_account_a)), n (store.confirmation_height.end ()); i != n && !finish_transaction;)
	{
		++read_operations;
		auto const & account (i->first);
		nano::block_hash hash (i->second.frontier);
		uint64_t depth (0);
		while (!hash.is_zero () && depth < max_depth_a)
		{
			auto block = ledger.any.block_get (transaction, hash);
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
				// FIXME: This is triggering an assertion where the iterator is still used after transaction is refreshed
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
			auto write_transaction = ledger.tx_begin_write ({ tables::blocks, tables::pruned }, nano::store::writer::pruning);
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
	auto bootstrap_weight_reached (ledger.block_count () >= ledger.bootstrap_weight_max_blocks);
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
		auto balance_scaled ((balance_l / nano::nano_ratio).convert_to<double> ());
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

std::optional<uint64_t> nano::node::work_generate_blocking (nano::block & block_a, uint64_t difficulty_a)
{
	auto opt_work_l (work_generate_blocking (block_a.work_version (), block_a.root (), difficulty_a, block_a.account_field ()));
	if (opt_work_l.has_value ())
	{
		block_a.block_work_set (opt_work_l.value ());
	}
	return opt_work_l;
}

void nano::node::work_generate (nano::work_version const version_a, nano::root const & root_a, uint64_t difficulty_a, std::function<void (std::optional<uint64_t>)> callback_a, std::optional<nano::account> const & account_a, bool secondary_work_peers_a)
{
	auto const & peers_l (secondary_work_peers_a ? config.secondary_work_peers : config.work_peers);
	if (distributed_work.make (version_a, root_a, peers_l, difficulty_a, callback_a, account_a))
	{
		// Error in creating the job (either stopped or work generation is not possible)
		callback_a (std::nullopt);
	}
}

std::optional<uint64_t> nano::node::work_generate_blocking (nano::work_version const version_a, nano::root const & root_a, uint64_t difficulty_a, std::optional<nano::account> const & account_a)
{
	std::promise<std::optional<uint64_t>> promise;
	work_generate (
	version_a, root_a, difficulty_a, [&promise] (std::optional<uint64_t> opt_work_a) {
		promise.set_value (opt_work_a);
	},
	account_a);
	return promise.get_future ().get ();
}

std::optional<uint64_t> nano::node::work_generate_blocking (nano::block & block_a)
{
	debug_assert (network_params.network.is_dev_network ());
	return work_generate_blocking (block_a, default_difficulty (nano::work_version::work_1));
}

std::optional<uint64_t> nano::node::work_generate_blocking (nano::root const & root_a)
{
	debug_assert (network_params.network.is_dev_network ());
	return work_generate_blocking (root_a, default_difficulty (nano::work_version::work_1));
}

std::optional<uint64_t> nano::node::work_generate_blocking (nano::root const & root_a, uint64_t difficulty_a)
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

	auto initial_peers = peer_history.peers ();

	logger.info (nano::log::type::node, "Adding cached initial peers: {}", initial_peers.size ());

	for (auto const & peer : initial_peers)
	{
		network.merge_peer (peer);
	}
}

void nano::node::start_election (std::shared_ptr<nano::block> const & block)
{
	scheduler.manual.push (block);
}

bool nano::node::block_confirmed (nano::block_hash const & hash_a)
{
	return ledger.confirmed.block_exists_or_pruned (ledger.tx_begin_read (), hash_a);
}

bool nano::node::block_confirmed_or_being_confirmed (nano::secure::transaction const & transaction, nano::block_hash const & hash_a)
{
	return confirming_set.exists (hash_a) || ledger.confirmed.block_exists_or_pruned (transaction, hash_a);
}

bool nano::node::block_confirmed_or_being_confirmed (nano::block_hash const & hash_a)
{
	return block_confirmed_or_being_confirmed (ledger.tx_begin_read (), hash_a);
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

void nano::node::process_confirmed (nano::election_status const & status_a, uint64_t iteration_a)
{
	auto hash (status_a.winner->hash ());
	decltype (iteration_a) const num_iters = (config.block_processor_batch_max_time / network_params.node.process_confirmed_interval) * 4;
	if (auto block_l = ledger.any.block_get (ledger.tx_begin_read (), hash))
	{
		logger.trace (nano::log::type::node, nano::log::detail::process_confirmed, nano::log::arg{ "block", block_l });

		confirming_set.add (block_l->hash ());
	}
	else if (iteration_a < num_iters)
	{
		iteration_a++;
		std::weak_ptr<nano::node> node_w (shared ());
		election_workers.add_timed_task (std::chrono::steady_clock::now () + network_params.node.process_confirmed_interval, [node_w, status_a, iteration_a] () {
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

std::pair<uint64_t, std::unordered_map<nano::account, nano::uint128_t>> nano::node::get_bootstrap_weights () const
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

nano::account nano::node::get_node_id () const
{
	return node_id.pub;
};

nano::telemetry_data nano::node::local_telemetry () const
{
	nano::telemetry_data telemetry_data;
	telemetry_data.node_id = node_id.pub;
	telemetry_data.block_count = ledger.block_count ();
	telemetry_data.cemented_count = ledger.cemented_count ();
	telemetry_data.bandwidth_cap = config.bandwidth_limit;
	telemetry_data.protocol_version = network_params.network.protocol_version;
	telemetry_data.uptime = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - startup_time).count ();
	telemetry_data.unchecked_count = unchecked.count ();
	telemetry_data.genesis_block = network_params.ledger.genesis->hash ();
	telemetry_data.peer_count = nano::narrow_cast<decltype (telemetry_data.peer_count)> (network.size ());
	telemetry_data.account_count = ledger.account_count ();
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

std::string nano::node::identifier () const
{
	return make_logger_identifier (node_id);
}

std::string nano::node::make_logger_identifier (const nano::keypair & node_id)
{
	// Node identifier consists of first 10 characters of node id
	return node_id.pub.to_node_id ().substr (0, 10);
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