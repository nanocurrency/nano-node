#include <nano/lib/threading.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/common.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/node.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/websocket.hpp>
#include <nano/rpc/rpc.hpp>
#include <nano/secure/buffer.hpp>
#include <nano/test_common/system.hpp>

#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cstdlib>
#include <future>
#include <sstream>

double constexpr nano::node::price_max;
double constexpr nano::node::free_cutoff;
size_t constexpr nano::block_arrival::arrival_size_min;
std::chrono::seconds constexpr nano::block_arrival::arrival_time_min;

namespace nano
{
extern unsigned char nano_bootstrap_weights_live[];
extern size_t nano_bootstrap_weights_live_size;
extern unsigned char nano_bootstrap_weights_beta[];
extern size_t nano_bootstrap_weights_beta_size;
}

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
					node_l->network.tcp_channels.start_tcp (endpoint, [node_w] (std::shared_ptr<nano::transport::channel> const & channel_a) {
						if (auto node_l = node_w.lock ())
						{
							node_l->network.send_keepalive (channel_a);
						}
					});
				}
				else
				{
					node_l->network.send_keepalive (channel);
				}
			}
		}
		else
		{
			node_l->logger.try_log (boost::str (boost::format ("Error resolving address: %1%:%2%: %3%") % address_a % port_a % ec.message ()));
		}
	});
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (rep_crawler & rep_crawler, std::string const & name)
{
	size_t count;
	{
		nano::lock_guard<nano::mutex> guard (rep_crawler.active_mutex);
		count = rep_crawler.active.size ();
	}

	auto sizeof_element = sizeof (decltype (rep_crawler.active)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "active", count, sizeof_element }));
	return composite;
}

nano::node::node (boost::asio::io_context & io_ctx_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, nano::logging const & logging_a, nano::work_pool & work_a, nano::node_flags flags_a, unsigned seq) :
	node (io_ctx_a, application_path_a, nano::node_config (peering_port_a, logging_a), work_a, flags_a, seq)
{
}

nano::node::node (boost::asio::io_context & io_ctx_a, boost::filesystem::path const & application_path_a, nano::node_config const & config_a, nano::work_pool & work_a, nano::node_flags flags_a, unsigned seq) :
	write_database_queue (!flags_a.force_use_write_database_queue && (config_a.rocksdb_config.enable)),
	io_ctx (io_ctx_a),
	node_initialized_latch (1),
	config (config_a),
	stats (config.stat_config),
	workers (std::max (3u, config.io_threads / 4), nano::thread_role::name::worker),
	flags (flags_a),
	work (work_a),
	distributed_work (*this),
	logger (config_a.logging.min_time_between_log_output),
	store_impl (nano::make_store (logger, application_path_a, flags.read_only, true, config_a.rocksdb_config, config_a.diagnostics_config.txn_tracking, config_a.block_processor_batch_max_time, config_a.lmdb_config, config_a.backup_before_upgrade)),
	store (*store_impl),
	wallets_store_impl (std::make_unique<nano::mdb_wallets_store> (application_path_a / "wallets.ldb", config_a.lmdb_config)),
	wallets_store (*wallets_store_impl),
	gap_cache (*this),
	ledger (store, stats, flags_a.generate_cache),
	checker (config.signature_checker_threads),
	network (*this, config.peering_port),
	telemetry (std::make_shared<nano::telemetry> (network, workers, observers.telemetry, stats, network_params, flags.disable_ongoing_telemetry_requests)),
	bootstrap_initiator (*this),
	bootstrap (config.peering_port, *this),
	application_path (application_path_a),
	port_mapping (*this),
	rep_crawler (*this),
	vote_processor (checker, active, observers, stats, config, flags, logger, online_reps, rep_crawler, ledger, network_params),
	warmed_up (0),
	block_processor (*this, write_database_queue),
	online_reps (ledger, config),
	history{ config.network_params.voting },
	vote_uniquer (block_uniquer),
	confirmation_height_processor (ledger, write_database_queue, config.conf_height_processor_batch_min_time, config.logging, logger, node_initialized_latch, flags.confirmation_height_processor_mode),
	active (*this, confirmation_height_processor),
	scheduler{ *this },
	aggregator (network_params.network, config, stats, active.generator, active.final_generator, history, ledger, wallets, active),
	wallets (wallets_store.init_error (), *this),
	startup_time (std::chrono::steady_clock::now ()),
	node_seq (seq)
{
	if (!init_error ())
	{
		telemetry->start ();

		active.vacancy_update = [this] () { scheduler.notify (); };

		if (config.websocket_config.enabled)
		{
			auto endpoint_l (nano::tcp_endpoint (boost::asio::ip::make_address_v6 (config.websocket_config.address), config.websocket_config.port));
			websocket_server = std::make_shared<nano::websocket::listener> (logger, wallets, io_ctx, endpoint_l);
			this->websocket_server->run ();
		}

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
			observers.blocks.add ([this] (nano::election_status const & status_a, std::vector<nano::vote_with_weight_info> const & votes_a, nano::account const & account_a, nano::amount const & amount_a, bool is_state_send_a) {
				auto block_a (status_a.winner);
				if ((status_a.type == nano::election_status_type::active_confirmed_quorum || status_a.type == nano::election_status_type::active_confirmation_height) && this->block_arrival.recent (block_a->hash ()))
				{
					auto node_l (shared_from_this ());
					background ([node_l, block_a, account_a, amount_a, is_state_send_a] () {
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
							else if (amount_a == 0 && node_l->ledger.is_epoch_link (block_a->link ()))
							{
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
								if (node_l->config.logging.callback_logging ())
								{
									node_l->logger.always_log (boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ()));
								}
								node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
							}
						});
					});
				}
			});
		}
		if (websocket_server)
		{
			observers.blocks.add ([this] (nano::election_status const & status_a, std::vector<nano::vote_with_weight_info> const & votes_a, nano::account const & account_a, nano::amount const & amount_a, bool is_state_send_a) {
				debug_assert (status_a.type != nano::election_status_type::ongoing);

				if (this->websocket_server->any_subscriber (nano::websocket::topic::confirmation))
				{
					auto block_a (status_a.winner);
					std::string subtype;
					if (is_state_send_a)
					{
						subtype = "send";
					}
					else if (block_a->type () == nano::block_type::state)
					{
						if (block_a->link ().is_zero ())
						{
							subtype = "change";
						}
						else if (amount_a == 0 && this->ledger.is_epoch_link (block_a->link ()))
						{
							subtype = "epoch";
						}
						else
						{
							subtype = "receive";
						}
					}

					this->websocket_server->broadcast_confirmation (block_a, account_a, amount_a, subtype, status_a, votes_a);
				}
			});

			observers.active_stopped.add ([this] (nano::block_hash const & hash_a) {
				if (this->websocket_server->any_subscriber (nano::websocket::topic::stopped_election))
				{
					nano::websocket::message_builder builder;
					this->websocket_server->broadcast (builder.stopped_election (hash_a));
				}
			});

			observers.telemetry.add ([this] (nano::telemetry_data const & telemetry_data, nano::endpoint const & endpoint) {
				if (this->websocket_server->any_subscriber (nano::websocket::topic::telemetry))
				{
					nano::websocket::message_builder builder;
					this->websocket_server->broadcast (builder.telemetry_received (telemetry_data, endpoint));
				}
			});
		}
		// Add block confirmation type stats regardless of http-callback and websocket subscriptions
		observers.blocks.add ([this] (nano::election_status const & status_a, std::vector<nano::vote_with_weight_info> const & votes_a, nano::account const & account_a, nano::amount const & amount_a, bool is_state_send_a) {
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
			if (channel_a->get_type () == nano::transport::transport_type::udp)
			{
				this->network.send_keepalive (channel_a);
			}
			else
			{
				this->network.send_keepalive_self (channel_a);
			}
		});
		observers.vote.add ([this] (std::shared_ptr<nano::vote> vote_a, std::shared_ptr<nano::transport::channel> const & channel_a, nano::vote_code code_a) {
			debug_assert (code_a != nano::vote_code::invalid);
			// The vote_code::vote is handled inside the election
			if (code_a == nano::vote_code::indeterminate)
			{
				auto active_in_rep_crawler (!this->rep_crawler.response (channel_a, vote_a));
				if (active_in_rep_crawler)
				{
					// Representative is defined as online if replying to live votes or rep_crawler queries
					this->online_reps.observe (vote_a->account);
				}
				this->gap_cache.vote (vote_a);
			}
		});
		if (websocket_server)
		{
			observers.vote.add ([this] (std::shared_ptr<nano::vote> vote_a, std::shared_ptr<nano::transport::channel> const & channel_a, nano::vote_code code_a) {
				if (this->websocket_server->any_subscriber (nano::websocket::topic::vote))
				{
					nano::websocket::message_builder builder;
					auto msg (builder.vote_received (vote_a, code_a));
					this->websocket_server->broadcast (msg);
				}
			});
		}
		// Cancelling local work generation
		observers.work_cancel.add ([this] (nano::root const & root_a) {
			this->work.cancel (root_a);
			this->distributed_work.cancel (root_a);
		});

		logger.always_log ("Node starting, version: ", NANO_VERSION_STRING);
		logger.always_log ("Build information: ", BUILD_INFO);
		logger.always_log ("Database backend: ", store.vendor_get ());

		auto network_label = network_params.network.get_current_network_as_string ();
		logger.always_log ("Active network: ", network_label);

		logger.always_log (boost::str (boost::format ("Work pool running %1% threads %2%") % work.threads.size () % (work.opencl ? "(1 for OpenCL)" : "")));
		logger.always_log (boost::str (boost::format ("%1% work peers configured") % config.work_peers.size ()));
		if (!work_generation_enabled ())
		{
			logger.always_log ("Work generation is disabled");
		}

		if (config.logging.node_lifetime_tracing ())
		{
			logger.always_log ("Constructing node");
		}

		logger.always_log (boost::str (boost::format ("Outbound Voting Bandwidth limited to %1% bytes per second, burst ratio %2%") % config.bandwidth_limit % config.bandwidth_limit_burst_ratio));

		// First do a pass with a read to see if any writing needs doing, this saves needing to open a write lock (and potentially blocking)
		auto is_initialized (false);
		{
			auto transaction (store.tx_begin_read ());
			is_initialized = (store.account.begin (transaction) != store.account.end ());
		}

		if (!is_initialized && !flags.read_only)
		{
			auto transaction (store.tx_begin_write ({ tables::accounts, tables::blocks, tables::confirmation_height, tables::frontiers }));
			// Store was empty meaning we just created it, add the genesis block
			store.initialize (transaction, ledger.cache);
		}

		if (!ledger.block_or_pruned_exists (network_params.ledger.genesis->hash ()))
		{
			std::stringstream ss;
			ss << "Genesis block not found. This commonly indicates a configuration issue, check that the --network or --data_path command line arguments are correct, "
				  "and also the ledger backend node config option. If using a read-only CLI command a ledger must already exist, start the node with --daemon first.";
			if (network_params.network.is_beta_network ())
			{
				ss << " Beta network may have reset, try clearing database files";
			}
			auto str = ss.str ();

			logger.always_log (str);
			std::cerr << str << std::endl;
			std::exit (1);
		}

		if (config.enable_voting)
		{
			std::ostringstream stream;
			stream << "Voting is enabled, more system resources will be used";
			auto voting (wallets.reps ().voting);
			if (voting > 0)
			{
				stream << ". " << voting << " representative(s) are configured";
				if (voting > 1)
				{
					stream << ". Voting with more than one representative can limit performance";
				}
			}
			logger.always_log (stream.str ());
		}

		node_id = nano::keypair ();
		logger.always_log ("Node ID: ", node_id.pub.to_node_id ());

		if ((network_params.network.is_live_network () || network_params.network.is_beta_network ()) && !flags.inactive_node)
		{
			auto bootstrap_weights = get_bootstrap_weights ();
			// Use bootstrap weights if initial bootstrap is not completed
			bool use_bootstrap_weight = ledger.cache.block_count < bootstrap_weights.first;
			if (use_bootstrap_weight)
			{
				ledger.bootstrap_weights = bootstrap_weights.second;
				for (auto const & rep : ledger.bootstrap_weights)
				{
					logger.always_log ("Using bootstrap rep weight: ", rep.first.to_account (), " -> ", nano::uint128_union (rep.second).format_balance (Mxrb_ratio, 0, true), " XRB");
				}
			}
			ledger.bootstrap_weight_max_blocks = bootstrap_weights.first;

			// Drop unchecked blocks if initial bootstrap is completed
			if (!flags.disable_unchecked_drop && !use_bootstrap_weight && !flags.read_only)
			{
				auto transaction (store.tx_begin_write ({ tables::unchecked }));
				store.unchecked.clear (transaction);
				logger.always_log ("Dropping unchecked blocks");
			}
		}

		ledger.pruning = flags.enable_pruning || store.pruned.count (store.tx_begin_read ()) > 0;

		if (ledger.pruning)
		{
			if (config.enable_voting && !flags.inactive_node)
			{
				std::string str = "Incompatibility detected between config node.enable_voting and existing pruned blocks";
				logger.always_log (str);
				std::cerr << str << std::endl;
				std::exit (1);
			}
			else if (!flags.enable_pruning && !flags.inactive_node)
			{
				std::string str = "To start node with existing pruned blocks use launch flag --enable_pruning";
				logger.always_log (str);
				std::cerr << str << std::endl;
				std::exit (1);
			}
		}
	}
	node_initialized_latch.count_down ();
}

nano::node::~node ()
{
	if (config.logging.node_lifetime_tracing ())
	{
		logger.always_log ("Destructing node");
	}
	stop ();
}

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
				boost::beast::http::async_write (*sock, *req, [node_l, sock, address, port, req, i_a, target, body, resolver] (boost::system::error_code const & ec, size_t bytes_transferred) mutable {
					if (!ec)
					{
						auto sb (std::make_shared<boost::beast::flat_buffer> ());
						auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
						boost::beast::http::async_read (*sock, *sb, *resp, [node_l, sb, resp, sock, address, port, i_a, target, body, resolver] (boost::system::error_code const & ec, size_t bytes_transferred) mutable {
							if (!ec)
							{
								if (boost::beast::http::to_status_class (resp->result ()) == boost::beast::http::status_class::successful)
								{
									node_l->stats.inc (nano::stat::type::http_callback, nano::stat::detail::initiate, nano::stat::dir::out);
								}
								else
								{
									if (node_l->config.logging.callback_logging ())
									{
										node_l->logger.try_log (boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ()));
									}
									node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
								}
							}
							else
							{
								if (node_l->config.logging.callback_logging ())
								{
									node_l->logger.try_log (boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ()));
								}
								node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
							};
						});
					}
					else
					{
						if (node_l->config.logging.callback_logging ())
						{
							node_l->logger.try_log (boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ()));
						}
						node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
					}
				});
			}
			else
			{
				if (node_l->config.logging.callback_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ()));
				}
				node_l->stats.inc (nano::stat::type::error, nano::stat::detail::http_callback, nano::stat::dir::out);
				++i_a;
				node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
			}
		});
	}
}

bool nano::node::copy_with_compaction (boost::filesystem::path const & destination)
{
	return store.copy_db (destination);
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (node & node, std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (collect_container_info (node.work, "work"));
	composite->add_component (collect_container_info (node.gap_cache, "gap_cache"));
	composite->add_component (collect_container_info (node.ledger, "ledger"));
	composite->add_component (collect_container_info (node.active, "active"));
	composite->add_component (collect_container_info (node.bootstrap_initiator, "bootstrap_initiator"));
	composite->add_component (collect_container_info (node.bootstrap, "bootstrap"));
	composite->add_component (collect_container_info (node.network, "network"));
	if (node.telemetry)
	{
		composite->add_component (collect_container_info (*node.telemetry, "telemetry"));
	}
	composite->add_component (collect_container_info (node.workers, "workers"));
	composite->add_component (collect_container_info (node.observers, "observers"));
	composite->add_component (collect_container_info (node.wallets, "wallets"));
	composite->add_component (collect_container_info (node.vote_processor, "vote_processor"));
	composite->add_component (collect_container_info (node.rep_crawler, "rep_crawler"));
	composite->add_component (collect_container_info (node.block_processor, "block_processor"));
	composite->add_component (collect_container_info (node.block_arrival, "block_arrival"));
	composite->add_component (collect_container_info (node.online_reps, "online_reps"));
	composite->add_component (collect_container_info (node.history, "history"));
	composite->add_component (collect_container_info (node.block_uniquer, "block_uniquer"));
	composite->add_component (collect_container_info (node.vote_uniquer, "vote_uniquer"));
	composite->add_component (collect_container_info (node.confirmation_height_processor, "confirmation_height_processor"));
	composite->add_component (collect_container_info (node.distributed_work, "distributed_work"));
	composite->add_component (collect_container_info (node.aggregator, "request_aggregator"));
	return composite;
}

void nano::node::process_active (std::shared_ptr<nano::block> const & incoming)
{
	block_arrival.add (incoming->hash ());
	block_processor.add (incoming, nano::seconds_since_epoch ());
}

nano::process_return nano::node::process (nano::block & block_a)
{
	auto transaction (store.tx_begin_write ({ tables::accounts, tables::blocks, tables::frontiers, tables::pending }));
	auto result (ledger.process (transaction, block_a));
	return result;
}

nano::process_return nano::node::process_local (std::shared_ptr<nano::block> const & block_a)
{
	// Add block hash as recently arrived to trigger automatic rebroadcast and election
	block_arrival.add (block_a->hash ());
	// Set current time to trigger automatic rebroadcast and election
	nano::unchecked_info info (block_a, block_a->account (), nano::seconds_since_epoch (), nano::signature_verification::unknown);
	// Notify block processor to release write lock
	block_processor.wait_write ();
	// Process block
	block_post_events post_events ([&store = store] { return store.tx_begin_read (); });
	auto transaction (store.tx_begin_write ({ tables::accounts, tables::blocks, tables::frontiers, tables::pending }));
	return block_processor.process_one (transaction, post_events, info, false, nano::block_origin::local);
}

void nano::node::process_local_async (std::shared_ptr<nano::block> const & block_a)
{
	// Add block hash as recently arrived to trigger automatic rebroadcast and election
	block_arrival.add (block_a->hash ());
	// Set current time to trigger automatic rebroadcast and election
	nano::unchecked_info info (block_a, block_a->account (), nano::seconds_since_epoch (), nano::signature_verification::unknown);
	block_processor.add_local (info);
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
	if (!flags.disable_unchecked_cleanup)
	{
		auto this_l (shared ());
		workers.push_task ([this_l] () {
			this_l->ongoing_unchecked_cleanup ();
		});
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
	bool tcp_enabled (false);
	if (config.tcp_incoming_connections_max > 0 && !(flags.disable_bootstrap_listener && flags.disable_tcp_realtime))
	{
		bootstrap.start ();
		tcp_enabled = true;
	}
	if (!flags.disable_backup)
	{
		backup_wallet ();
	}
	if (!flags.disable_search_pending)
	{
		search_pending ();
	}
	if (!flags.disable_wallet_bootstrap)
	{
		// Delay to start wallet lazy bootstrap
		auto this_l (shared ());
		workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::minutes (1), [this_l] () {
			this_l->bootstrap_wallet ();
		});
	}
	// Start port mapping if external address is not defined and TCP or UDP ports are enabled
	if (config.external_address == boost::asio::ip::address_v6{}.any ().to_string () && (tcp_enabled || !flags.disable_udp))
	{
		port_mapping.start ();
	}
	if (config.frontiers_confirmation != nano::frontiers_confirmation_mode::disabled)
	{
		workers.push_task ([this_l = shared ()] () {
			this_l->ongoing_backlog_population ();
		});
	}
}

void nano::node::stop ()
{
	if (!stopped.exchange (true))
	{
		logger.always_log ("Node stopping");
		// Cancels ongoing work generation tasks, which may be blocking other threads
		// No tasks may wait for work generation in I/O threads, or termination signal capturing will be unable to call node::stop()
		distributed_work.stop ();
		block_processor.stop ();
		aggregator.stop ();
		vote_processor.stop ();
		scheduler.stop ();
		active.stop ();
		confirmation_height_processor.stop ();
		network.stop ();
		telemetry->stop ();
		if (websocket_server)
		{
			websocket_server->stop ();
		}
		bootstrap_initiator.stop ();
		bootstrap.stop ();
		port_mapping.stop ();
		checker.stop ();
		wallets.stop ();
		stats.stop ();
		auto epoch_upgrade = epoch_upgrading.lock ();
		if (epoch_upgrade->valid ())
		{
			epoch_upgrade->wait ();
		}
		workers.stop ();
		// work pool is not stopped on purpose due to testing setup
	}
}

void nano::node::keepalive_preconfigured (std::vector<std::string> const & peers_a)
{
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		keepalive (*i, network_params.network.default_node_port);
	}
}

nano::block_hash nano::node::latest (nano::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.latest (transaction, account_a);
}

nano::uint128_t nano::node::balance (nano::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.account_balance (transaction, account_a);
}

std::shared_ptr<nano::block> nano::node::block (nano::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return store.block.get (transaction, hash_a);
}

std::pair<nano::uint128_t, nano::uint128_t> nano::node::balance_pending (nano::account const & account_a, bool only_confirmed_a)
{
	std::pair<nano::uint128_t, nano::uint128_t> result;
	auto transaction (store.tx_begin_read ());
	result.first = ledger.account_balance (transaction, account_a, only_confirmed_a);
	result.second = ledger.account_pending (transaction, account_a, only_confirmed_a);
	return result;
}

nano::uint128_t nano::node::weight (nano::account const & account_a)
{
	return ledger.weight (account_a);
}

nano::block_hash nano::node::rep_block (nano::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	nano::account_info info;
	nano::block_hash result (0);
	if (!store.account.get (transaction, account_a, info))
	{
		result = ledger.representative (transaction, info.head);
	}
	return result;
}

nano::uint128_t nano::node::minimum_principal_weight ()
{
	return minimum_principal_weight (online_reps.trended ());
}

nano::uint128_t nano::node::minimum_principal_weight (nano::uint128_t const & online_stake)
{
	return online_stake / network_params.network.principal_weight_factor;
}

void nano::node::long_inactivity_cleanup ()
{
	bool perform_cleanup = false;
	auto transaction (store.tx_begin_write ({ tables::online_weight, tables::peers }));
	if (store.online_weight.count (transaction) > 0)
	{
		auto sample (store.online_weight.rbegin (transaction));
		auto n (store.online_weight.end ());
		debug_assert (sample != n);
		auto const one_week_ago = static_cast<size_t> ((std::chrono::system_clock::now () - std::chrono::hours (7 * 24)).time_since_epoch ().count ());
		perform_cleanup = sample->first < one_week_ago;
	}
	if (perform_cleanup)
	{
		store.online_weight.clear (transaction);
		store.peer.clear (transaction);
		logger.always_log ("Removed records of peers and online weight after a long period of inactivity");
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
	auto next_wakeup (network_params.node.bootstrap_interval);
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
			uint64_t time_since_last_sample = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count () - last_sample_time / std::pow (10, 9); // Nanoseconds to seconds
			if (time_since_last_sample + 60 * 60 < std::numeric_limits<uint32_t>::max ())
			{
				frontiers_age = std::max<uint32_t> (time_since_last_sample + 60 * 60, network_params.bootstrap.default_frontiers_age_seconds);
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
	bool stored (network.tcp_channels.store_all (true));
	network.udp_channels.store_all (!stored);
	std::weak_ptr<nano::node> node_w (shared_from_this ());
	workers.add_timed_task (std::chrono::steady_clock::now () + network_params.node.peer_interval, [node_w] () {
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

		boost::filesystem::create_directories (backup_path);
		nano::set_secure_perm_directory (backup_path, error_chmod);
		i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
	}
	auto this_l (shared ());
	workers.add_timed_task (std::chrono::steady_clock::now () + network_params.node.backup_interval, [this_l] () {
		this_l->backup_wallet ();
	});
}

void nano::node::search_pending ()
{
	// Reload wallets from disk
	wallets.reload ();
	// Search pending
	wallets.search_pending_all ();
	auto this_l (shared ());
	workers.add_timed_task (std::chrono::steady_clock::now () + network_params.node.search_pending_interval, [this_l] () {
		this_l->search_pending ();
	});
}

void nano::node::bootstrap_wallet ()
{
	std::deque<nano::account> accounts;
	{
		nano::lock_guard<nano::mutex> lock (wallets.mutex);
		auto transaction (wallets.tx_begin_read ());
		for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n && accounts.size () < 128; ++i)
		{
			auto & wallet (*i->second);
			nano::lock_guard<std::recursive_mutex> wallet_lock (wallet.store.mutex);
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

void nano::node::unchecked_cleanup ()
{
	std::vector<nano::uint128_t> digests;
	std::deque<nano::unchecked_key> cleaning_list;
	auto attempt (bootstrap_initiator.current_attempt ());
	bool long_attempt (attempt != nullptr && std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - attempt->attempt_start).count () > config.unchecked_cutoff_time.count ());
	// Collect old unchecked keys
	if (ledger.cache.block_count >= ledger.bootstrap_weight_max_blocks && !long_attempt)
	{
		auto now (nano::seconds_since_epoch ());
		auto transaction (store.tx_begin_read ());
		// Max 1M records to clean, max 2 minutes reading to prevent slow i/o systems issues
		for (auto i (store.unchecked.begin (transaction)), n (store.unchecked.end ()); i != n && cleaning_list.size () < 1024 * 1024 && nano::seconds_since_epoch () - now < 120; ++i)
		{
			nano::unchecked_key const & key (i->first);
			nano::unchecked_info const & info (i->second);
			if ((now - info.modified) > static_cast<uint64_t> (config.unchecked_cutoff_time.count ()))
			{
				digests.push_back (network.publish_filter.hash (info.block));
				cleaning_list.push_back (key);
			}
		}
	}
	if (!cleaning_list.empty ())
	{
		logger.always_log (boost::str (boost::format ("Deleting %1% old unchecked blocks") % cleaning_list.size ()));
	}
	// Delete old unchecked keys in batches
	while (!cleaning_list.empty ())
	{
		size_t deleted_count (0);
		auto transaction (store.tx_begin_write ({ tables::unchecked }));
		while (deleted_count++ < 2 * 1024 && !cleaning_list.empty ())
		{
			auto key (cleaning_list.front ());
			cleaning_list.pop_front ();
			if (store.unchecked.exists (transaction, key))
			{
				store.unchecked.del (transaction, key);
			}
		}
	}
	// Delete from the duplicate filter
	network.publish_filter.clear (digests);
}

void nano::node::ongoing_unchecked_cleanup ()
{
	unchecked_cleanup ();
	workers.add_timed_task (std::chrono::steady_clock::now () + network_params.node.unchecked_cleaning_interval, [this_l = shared ()] () {
		this_l->ongoing_unchecked_cleanup ();
	});
}

void nano::node::ongoing_backlog_population ()
{
	populate_backlog ();
	auto delay = config.network_params.network.is_dev_network () ? std::chrono::seconds{ 1 } : std::chrono::duration_cast<std::chrono::seconds> (std::chrono::minutes{ 5 });
	workers.add_timed_task (std::chrono::steady_clock::now () + delay, [this_l = shared ()] () {
		this_l->ongoing_backlog_population ();
	});
}

bool nano::node::collect_ledger_pruning_targets (std::deque<nano::block_hash> & pruning_targets_a, nano::account & last_account_a, uint64_t const batch_read_size_a, uint64_t const max_depth_a, uint64_t const cutoff_time_a)
{
	uint64_t read_operations (0);
	bool finish_transaction (false);
	auto transaction (store.tx_begin_read ());
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

void nano::node::ledger_pruning (uint64_t const batch_size_a, bool bootstrap_weight_reached_a, bool log_to_cout_a)
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
			auto log_message (boost::str (boost::format ("%1% blocks pruned") % pruned_count));
			if (!log_to_cout_a)
			{
				logger.try_log (log_message);
			}
			else
			{
				std::cout << log_message << std::endl;
			}
		}
	}
	auto log_message (boost::str (boost::format ("Total recently pruned block count: %1%") % pruned_count));
	if (!log_to_cout_a)
	{
		logger.always_log (log_message);
	}
	else
	{
		std::cout << log_message << std::endl;
	}
}

void nano::node::ongoing_ledger_pruning ()
{
	auto bootstrap_weight_reached (ledger.cache.block_count >= ledger.bootstrap_weight_max_blocks);
	ledger_pruning (flags.block_processor_batch_size != 0 ? flags.block_processor_batch_size : 2 * 1024, bootstrap_weight_reached, false);
	auto ledger_pruning_interval (bootstrap_weight_reached ? config.max_pruning_age : std::min (config.max_pruning_age, std::chrono::seconds (15 * 60)));
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
			result = nano::work_threshold_base (version_a);
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
			result = network_params.network.publish_thresholds.epoch_2_receive;
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
	auto transaction (store.tx_begin_read ());
	for (auto i (store.peer.begin (transaction)), n (store.peer.end ()); i != n; ++i)
	{
		nano::endpoint endpoint (boost::asio::ip::address_v6 (i->first.address_bytes ()), i->first.port ());
		if (!network.reachout (endpoint, config.allow_local_peers))
		{
			std::weak_ptr<nano::node> node_w (shared_from_this ());
			network.tcp_channels.start_tcp (endpoint, [node_w] (std::shared_ptr<nano::transport::channel> const & channel_a) {
				if (auto node_l = node_w.lock ())
				{
					node_l->network.send_keepalive (channel_a);
					if (!node_l->flags.disable_rep_crawler)
					{
						node_l->rep_crawler.query (channel_a);
					}
				}
			});
		}
	}
}

void nano::node::block_confirm (std::shared_ptr<nano::block> const & block_a)
{
	scheduler.manual (block_a);
	scheduler.flush ();
	auto election = active.election (block_a->qualified_root ());
	if (election != nullptr)
	{
		election->transition_active ();
	}
}

bool nano::node::block_confirmed (nano::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.block_confirmed (transaction, hash_a);
}

bool nano::node::block_confirmed_or_being_confirmed (nano::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	return confirmation_height_processor.is_processing_block (hash_a) || ledger.block_confirmed (transaction_a, hash_a);
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

void nano::node::receive_confirmed (nano::transaction const & block_transaction_a, nano::block_hash const & hash_a, nano::account const & destination_a)
{
	nano::unique_lock<nano::mutex> lk (wallets.mutex);
	auto wallets_l = wallets.get_wallets ();
	auto wallet_transaction = wallets.tx_begin_read ();
	lk.unlock ();
	for ([[maybe_unused]] auto const & [id, wallet] : wallets_l)
	{
		if (wallet->store.exists (wallet_transaction, destination_a))
		{
			nano::account representative;
			nano::pending_info pending;
			representative = wallet->store.representative (wallet_transaction);
			auto error (store.pending.get (block_transaction_a, nano::pending_key (destination_a, hash_a), pending));
			if (!error)
			{
				auto amount (pending.amount.number ());
				wallet->receive_async (hash_a, representative, amount, destination_a, [] (std::shared_ptr<nano::block> const &) {});
			}
			else
			{
				if (!ledger.block_or_pruned_exists (block_transaction_a, hash_a))
				{
					logger.try_log (boost::str (boost::format ("Confirmed block is missing:  %1%") % hash_a.to_string ()));
					debug_assert (false && "Confirmed block is missing");
				}
				else
				{
					logger.try_log (boost::str (boost::format ("Block %1% has already been received") % hash_a.to_string ()));
				}
			}
		}
	}
}

void nano::node::process_confirmed_data (nano::transaction const & transaction_a, std::shared_ptr<nano::block> const & block_a, nano::block_hash const & hash_a, nano::account & account_a, nano::uint128_t & amount_a, bool & is_state_send_a, nano::account & pending_account_a)
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
	auto block_balance (store.block.balance_calculated (block_a));
	if (hash_a != ledger.network_params.ledger.genesis_account ())
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
		amount_a = nano::dev::genesis_amount;
	}
	if (auto state = dynamic_cast<nano::state_block *> (block_a.get ()))
	{
		if (state->hashables.balance < previous_balance)
		{
			is_state_send_a = true;
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
	const auto num_iters = (config.block_processor_batch_max_time / network_params.node.process_confirmed_interval) * 4;
	if (auto block_l = ledger.store.block.get (ledger.store.tx_begin_read (), hash))
	{
		active.add_recently_confirmed (block_l->qualified_root (), hash);
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

bool nano::block_arrival::add (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	auto inserted (arrival.get<tag_sequence> ().emplace_back (nano::block_arrival_info{ now, hash_a }));
	auto result (!inserted.second);
	return result;
}

bool nano::block_arrival::recent (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	while (arrival.size () > arrival_size_min && arrival.get<tag_sequence> ().front ().arrival + arrival_time_min < now)
	{
		arrival.get<tag_sequence> ().pop_front ();
	}
	return arrival.get<tag_hash> ().find (hash_a) != arrival.get<tag_hash> ().end ();
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (block_arrival & block_arrival, std::string const & name)
{
	size_t count = 0;
	{
		nano::lock_guard<nano::mutex> guard (block_arrival.mutex);
		count = block_arrival.arrival.size ();
	}

	auto sizeof_element = sizeof (decltype (block_arrival.arrival)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "arrival", count, sizeof_element }));
	return composite;
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

bool nano::node::epoch_upgrader (nano::raw_key const & prv_a, nano::epoch epoch_a, uint64_t count_limit, uint64_t threads)
{
	bool error = stopped.load ();
	if (!error)
	{
		auto epoch_upgrade = epoch_upgrading.lock ();
		error = epoch_upgrade->valid () && epoch_upgrade->wait_for (std::chrono::seconds (0)) == std::future_status::timeout;
		if (!error)
		{
			*epoch_upgrade = std::async (std::launch::async, &nano::node::epoch_upgrader_impl, this, prv_a, epoch_a, count_limit, threads);
		}
	}
	return error;
}

void nano::node::set_bandwidth_params (size_t limit, double ratio)
{
	config.bandwidth_limit_burst_ratio = ratio;
	config.bandwidth_limit = limit;
	network.set_bandwidth_params (limit, ratio);
	logger.always_log (boost::str (boost::format ("set_bandwidth_params(%1%, %2%)") % limit % ratio));
}

void nano::node::epoch_upgrader_impl (nano::raw_key const & prv_a, nano::epoch epoch_a, uint64_t count_limit, uint64_t threads)
{
	nano::thread_role::set (nano::thread_role::name::epoch_upgrader);
	auto upgrader_process = [] (nano::node & node_a, std::atomic<uint64_t> & counter, std::shared_ptr<nano::block> const & epoch, uint64_t difficulty, nano::public_key const & signer_a, nano::root const & root_a, nano::account const & account_a) {
		epoch->block_work_set (node_a.work_generate_blocking (nano::work_version::work_1, root_a, difficulty).value_or (0));
		bool valid_signature (!nano::validate_message (signer_a, epoch->hash (), epoch->block_signature ()));
		bool valid_work (epoch->difficulty () >= difficulty);
		nano::process_result result (nano::process_result::old);
		if (valid_signature && valid_work)
		{
			result = node_a.process_local (epoch).code;
		}
		if (result == nano::process_result::progress)
		{
			++counter;
		}
		else
		{
			bool fork (result == nano::process_result::fork);
			node_a.logger.always_log (boost::str (boost::format ("Failed to upgrade account %1%. Valid signature: %2%. Valid work: %3%. Block processor fork: %4%") % account_a.to_account () % valid_signature % valid_work % fork));
		}
	};

	uint64_t const upgrade_batch_size = 1000;
	nano::block_builder builder;
	auto link (ledger.epoch_link (epoch_a));
	nano::raw_key raw_key;
	raw_key = prv_a;
	auto signer (nano::pub_key (prv_a));
	debug_assert (signer == ledger.epoch_signer (link));

	nano::mutex upgrader_mutex;
	nano::condition_variable upgrader_condition;

	class account_upgrade_item final
	{
	public:
		nano::account account{ 0 };
		uint64_t modified{ 0 };
	};
	class account_tag
	{
	};
	class modified_tag
	{
	};
	// clang-format off
	boost::multi_index_container<account_upgrade_item,
	boost::multi_index::indexed_by<
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<modified_tag>,
			boost::multi_index::member<account_upgrade_item, uint64_t, &account_upgrade_item::modified>,
			std::greater<uint64_t>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<account_tag>,
			boost::multi_index::member<account_upgrade_item, nano::account, &account_upgrade_item::account>>>>
	accounts_list;
	// clang-format on

	bool finished_upgrade (false);

	while (!finished_upgrade && !stopped)
	{
		bool finished_accounts (false);
		uint64_t total_upgraded_accounts (0);
		while (!finished_accounts && count_limit != 0 && !stopped)
		{
			{
				auto transaction (store.tx_begin_read ());
				// Collect accounts to upgrade
				for (auto i (store.account.begin (transaction)), n (store.account.end ()); i != n && accounts_list.size () < count_limit; ++i)
				{
					nano::account const & account (i->first);
					nano::account_info const & info (i->second);
					if (info.epoch () < epoch_a)
					{
						release_assert (nano::epochs::is_sequential (info.epoch (), epoch_a));
						accounts_list.emplace (account_upgrade_item{ account, info.modified });
					}
				}
			}

			/* Upgrade accounts
			Repeat until accounts with previous epoch exist in latest table */
			std::atomic<uint64_t> upgraded_accounts (0);
			uint64_t workers (0);
			uint64_t attempts (0);
			for (auto i (accounts_list.get<modified_tag> ().begin ()), n (accounts_list.get<modified_tag> ().end ()); i != n && attempts < upgrade_batch_size && attempts < count_limit && !stopped; ++i)
			{
				auto transaction (store.tx_begin_read ());
				nano::account_info info;
				nano::account const & account (i->account);
				if (!store.account.get (transaction, account, info) && info.epoch () < epoch_a)
				{
					++attempts;
					auto difficulty (nano::work_threshold (nano::work_version::work_1, nano::block_details (epoch_a, false, false, true)));
					nano::root const & root (info.head);
					std::shared_ptr<nano::block> epoch = builder.state ()
														 .account (account)
														 .previous (info.head)
														 .representative (info.representative)
														 .balance (info.balance)
														 .link (link)
														 .sign (raw_key, signer)
														 .work (0)
														 .build ();
					if (threads != 0)
					{
						{
							nano::unique_lock<nano::mutex> lock (upgrader_mutex);
							++workers;
							while (workers > threads)
							{
								upgrader_condition.wait (lock);
							}
						}
						this->workers.push_task ([node_l = shared_from_this (), &upgrader_process, &upgrader_mutex, &upgrader_condition, &upgraded_accounts, &workers, epoch, difficulty, signer, root, account] () {
							upgrader_process (*node_l, upgraded_accounts, epoch, difficulty, signer, root, account);
							{
								nano::lock_guard<nano::mutex> lock (upgrader_mutex);
								--workers;
							}
							upgrader_condition.notify_all ();
						});
					}
					else
					{
						upgrader_process (*this, upgraded_accounts, epoch, difficulty, signer, root, account);
					}
				}
			}
			{
				nano::unique_lock<nano::mutex> lock (upgrader_mutex);
				while (workers > 0)
				{
					upgrader_condition.wait (lock);
				}
			}
			total_upgraded_accounts += upgraded_accounts;
			count_limit -= upgraded_accounts;

			if (!accounts_list.empty ())
			{
				logger.always_log (boost::str (boost::format ("%1% accounts were upgraded to new epoch, %2% remain...") % total_upgraded_accounts % (accounts_list.size () - upgraded_accounts)));
				accounts_list.clear ();
			}
			else
			{
				logger.always_log (boost::str (boost::format ("%1% total accounts were upgraded to new epoch") % total_upgraded_accounts));
				finished_accounts = true;
			}
		}

		// Pending blocks upgrade
		bool finished_pending (false);
		uint64_t total_upgraded_pending (0);
		while (!finished_pending && count_limit != 0 && !stopped)
		{
			std::atomic<uint64_t> upgraded_pending (0);
			uint64_t workers (0);
			uint64_t attempts (0);
			auto transaction (store.tx_begin_read ());
			for (auto i (store.pending.begin (transaction, nano::pending_key (1, 0))), n (store.pending.end ()); i != n && attempts < upgrade_batch_size && attempts < count_limit && !stopped;)
			{
				bool to_next_account (false);
				nano::pending_key const & key (i->first);
				if (!store.account.exists (transaction, key.account))
				{
					nano::pending_info const & info (i->second);
					if (info.epoch < epoch_a)
					{
						++attempts;
						release_assert (nano::epochs::is_sequential (info.epoch, epoch_a));
						auto difficulty (nano::work_threshold (nano::work_version::work_1, nano::block_details (epoch_a, false, false, true)));
						nano::root const & root (key.account);
						nano::account const & account (key.account);
						std::shared_ptr<nano::block> epoch = builder.state ()
															 .account (key.account)
															 .previous (0)
															 .representative (0)
															 .balance (0)
															 .link (link)
															 .sign (raw_key, signer)
															 .work (0)
															 .build ();
						if (threads != 0)
						{
							{
								nano::unique_lock<nano::mutex> lock (upgrader_mutex);
								++workers;
								while (workers > threads)
								{
									upgrader_condition.wait (lock);
								}
							}
							this->workers.push_task ([node_l = shared_from_this (), &upgrader_process, &upgrader_mutex, &upgrader_condition, &upgraded_pending, &workers, epoch, difficulty, signer, root, account] () {
								upgrader_process (*node_l, upgraded_pending, epoch, difficulty, signer, root, account);
								{
									nano::lock_guard<nano::mutex> lock (upgrader_mutex);
									--workers;
								}
								upgrader_condition.notify_all ();
							});
						}
						else
						{
							upgrader_process (*this, upgraded_pending, epoch, difficulty, signer, root, account);
						}
					}
				}
				else
				{
					to_next_account = true;
				}
				if (to_next_account)
				{
					// Move to next account if pending account exists or was upgraded
					if (key.account.number () == std::numeric_limits<nano::uint256_t>::max ())
					{
						break;
					}
					else
					{
						i = store.pending.begin (transaction, nano::pending_key (key.account.number () + 1, 0));
					}
				}
				else
				{
					// Move to next pending item
					++i;
				}
			}
			{
				nano::unique_lock<nano::mutex> lock (upgrader_mutex);
				while (workers > 0)
				{
					upgrader_condition.wait (lock);
				}
			}

			total_upgraded_pending += upgraded_pending;
			count_limit -= upgraded_pending;

			// Repeat if some pending accounts were upgraded
			if (upgraded_pending != 0)
			{
				logger.always_log (boost::str (boost::format ("%1% unopened accounts with pending blocks were upgraded to new epoch...") % total_upgraded_pending));
			}
			else
			{
				logger.always_log (boost::str (boost::format ("%1% total unopened accounts with pending blocks were upgraded to new epoch") % total_upgraded_pending));
				finished_pending = true;
			}
		}

		finished_upgrade = (total_upgraded_accounts == 0) && (total_upgraded_pending == 0);
	}

	logger.always_log ("Epoch upgrade is completed");
}

std::pair<uint64_t, decltype (nano::ledger::bootstrap_weights)> nano::node::get_bootstrap_weights () const
{
	std::unordered_map<nano::account, nano::uint128_t> weights;
	const uint8_t * weight_buffer = network_params.network.is_live_network () ? nano_bootstrap_weights_live : nano_bootstrap_weights_beta;
	size_t weight_size = network_params.network.is_live_network () ? nano_bootstrap_weights_live_size : nano_bootstrap_weights_beta_size;
	nano::bufferstream weight_stream ((const uint8_t *)weight_buffer, weight_size);
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

void nano::node::populate_backlog ()
{
	auto done = false;
	uint64_t const chunk_size = 65536;
	nano::account next = 0;
	uint64_t total = 0;
	while (!stopped && !done)
	{
		auto transaction = store.tx_begin_read ();
		auto count = 0;
		for (auto i = store.account.begin (transaction, next), n = store.account.end (); !stopped && i != n && count < chunk_size; ++i, ++count, ++total)
		{
			auto const & account = i->first;
			scheduler.activate (account, transaction);
			next = account.number () + 1;
		}
		done = store.account.begin (transaction, next) == store.account.end ();
	}
}

nano::node_wrapper::node_wrapper (boost::filesystem::path const & path_a, boost::filesystem::path const & config_path_a, nano::node_flags const & node_flags_a) :
	io_context (std::make_shared<boost::asio::io_context> ()),
	work (1)
{
	boost::system::error_code error_chmod;

	/*
	 * @warning May throw a filesystem exception
	 */
	boost::filesystem::create_directories (path_a);
	nano::set_secure_perm_directory (path_a, error_chmod);
	nano::daemon_config daemon_config (path_a);
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
	node_config.logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
	node_config.logging.init (path_a);

	node = std::make_shared<nano::node> (*io_context, path_a, node_config, work, node_flags_a);
}

nano::node_wrapper::~node_wrapper ()
{
	node->stop ();
}

nano::inactive_node::inactive_node (boost::filesystem::path const & path_a, boost::filesystem::path const & config_path_a, nano::node_flags const & node_flags_a) :
	node_wrapper (path_a, config_path_a, node_flags_a),
	node (node_wrapper.node)
{
	node_wrapper.node->active.stop ();
}

nano::inactive_node::inactive_node (boost::filesystem::path const & path_a, nano::node_flags const & node_flags_a) :
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

std::unique_ptr<nano::store> nano::make_store (nano::logger_mt & logger, boost::filesystem::path const & path, bool read_only, bool add_db_postfix, nano::rocksdb_config const & rocksdb_config, nano::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, nano::lmdb_config const & lmdb_config_a, bool backup_before_upgrade)
{
	if (rocksdb_config.enable)
	{
		return std::make_unique<nano::rocksdb_store> (logger, add_db_postfix ? path / "rocksdb" : path, rocksdb_config, read_only);
	}

	return std::make_unique<nano::mdb_store> (logger, add_db_postfix ? path / "data.ldb" : path, txn_tracking_config_a, block_processor_batch_max_time_a, lmdb_config_a, backup_before_upgrade);
}
