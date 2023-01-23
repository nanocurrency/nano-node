#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/format.hpp>

#include <cryptopp/words.h>

namespace
{
char const * preconfigured_peers_key = "preconfigured_peers";
char const * signature_checker_threads_key = "signature_checker_threads";
char const * pow_sleep_interval_key = "pow_sleep_interval";
std::string const default_live_peer_network = nano::get_env_or_default ("NANO_DEFAULT_PEER", "peering.nano.org");
std::string const default_beta_peer_network = nano::get_env_or_default ("NANO_DEFAULT_PEER", "peering-beta.nano.org");
std::string const default_test_peer_network = nano::get_env_or_default ("NANO_DEFAULT_PEER", "peering-test.nano.org");
}

nano::node_config::node_config (nano::network_params & network_params) :
	node_config (std::nullopt, nano::logging (), network_params)
{
}

nano::node_config::node_config (const std::optional<uint16_t> & peering_port_a, nano::logging const & logging_a, nano::network_params & network_params) :
	network_params{ network_params },
	peering_port{ peering_port_a },
	logging{ logging_a },
	websocket_config{ network_params.network },
	ipc_config{ network_params.network },
	external_address{ boost::asio::ip::address_v6{}.to_string () }
{
	if (peering_port == 0)
	{
		// comment for posterity:
		// - we used to consider ports being 0 a sentinel that meant to use a default port for that specific purpose
		// - the actual default value was determined based on the active network (e.g. dev network peering port = 44000)
		// - now, the 0 value means something different instead: user wants to let the OS pick a random port
		// - for the specific case of the peering port, after it gets picked, it can be retrieved by client code via
		//   node.network.endpoint ().port ()
		// - the config value does not get back-propagated because it represents the choice of the user, and that was 0
	}

	switch (network_params.network.network ())
	{
		case nano::networks::nano_dev_network:
			enable_voting = true;
			preconfigured_representatives.push_back (network_params.ledger.genesis->account ());
			break;
		case nano::networks::nano_beta_network:
		{
			preconfigured_peers.emplace_back (default_beta_peer_network);
			nano::account offline_representative;
			release_assert (!offline_representative.decode_account ("nano_1defau1t9off1ine9rep99999999999999999999999999999999wgmuzxxy"));
			preconfigured_representatives.emplace_back (offline_representative);
			break;
		}
		case nano::networks::nano_live_network:
			preconfigured_peers.emplace_back (default_live_peer_network);
			preconfigured_representatives.emplace_back ("A30E0A32ED41C8607AA9212843392E853FCBCB4E7CB194E35C94F07F91DE59EF");
			preconfigured_representatives.emplace_back ("67556D31DDFC2A440BF6147501449B4CB9572278D034EE686A6BEE29851681DF");
			preconfigured_representatives.emplace_back ("5C2FBB148E006A8E8BA7A75DD86C9FE00C83F5FFDBFD76EAA09531071436B6AF");
			preconfigured_representatives.emplace_back ("AE7AC63990DAAAF2A69BF11C913B928844BF5012355456F2F164166464024B29");
			preconfigured_representatives.emplace_back ("BD6267D6ECD8038327D2BCC0850BDF8F56EC0414912207E81BCF90DFAC8A4AAA");
			preconfigured_representatives.emplace_back ("2399A083C600AA0572F5E36247D978FCFC840405F8D4B6D33161C0066A55F431");
			preconfigured_representatives.emplace_back ("2298FAB7C61058E77EA554CB93EDEEDA0692CBFCC540AB213B2836B29029E23A");
			preconfigured_representatives.emplace_back ("3FE80B4BC842E82C1C18ABFEEC47EA989E63953BC82AC411F304D13833D52A56");
			break;
		case nano::networks::nano_test_network:
			preconfigured_peers.push_back (default_test_peer_network);
			preconfigured_representatives.push_back (network_params.ledger.genesis->account ());
			break;
		default:
			debug_assert (false);
			break;
	}
}

nano::error nano::node_config::serialize_toml (nano::tomlconfig & toml) const
{
	if (peering_port.has_value ())
	{
		toml.put ("peering_port", *peering_port, "Node peering port.\ntype:uint16");
	}

	toml.put ("bootstrap_fraction_numerator", bootstrap_fraction_numerator, "Change bootstrap threshold (online stake / 256 * bootstrap_fraction_numerator).\ntype:uint32");
	toml.put ("receive_minimum", receive_minimum.to_string_dec (), "Minimum receive amount. Only affects node wallets. A large amount is recommended to avoid automatic work generation for tiny transactions.\ntype:string,amount,raw");
	toml.put ("online_weight_minimum", online_weight_minimum.to_string_dec (), "When calculating online weight, the node is forced to assume at least this much voting weight is online, thus setting a floor for voting weight to confirm transactions at online_weight_minimum * \"quorum delta\".\ntype:string,amount,raw");
	toml.put ("election_hint_weight_percent", election_hint_weight_percent, "Percentage of online weight to hint at starting an election. Defaults to 10.\ntype:uint32,[5,50]");
	toml.put ("password_fanout", password_fanout, "Password fanout factor.\ntype:uint64");
	toml.put ("io_threads", io_threads, "Number of threads dedicated to I/O operations. Defaults to the number of CPU threads, and at least 4.\ntype:uint64");
	toml.put ("network_threads", network_threads, "Number of threads dedicated to processing network messages. Defaults to the number of CPU threads, and at least 4.\ntype:uint64");
	toml.put ("work_threads", work_threads, "Number of threads dedicated to CPU generated work. Defaults to all available CPU threads.\ntype:uint64");
	toml.put ("signature_checker_threads", signature_checker_threads, "Number of additional threads dedicated to signature verification. Defaults to number of CPU threads / 2.\ntype:uint64");
	toml.put ("enable_voting", enable_voting, "Enable or disable voting. Enabling this option requires additional system resources, namely increased CPU, bandwidth and disk usage.\ntype:bool");
	toml.put ("bootstrap_connections", bootstrap_connections, "Number of outbound bootstrap connections. Must be a power of 2. Defaults to 4.\nWarning: a larger amount of connections may use substantially more system memory.\ntype:uint64");
	toml.put ("bootstrap_connections_max", bootstrap_connections_max, "Maximum number of inbound bootstrap connections. Defaults to 64.\nWarning: a larger amount of connections may use additional system memory.\ntype:uint64");
	toml.put ("bootstrap_initiator_threads", bootstrap_initiator_threads, "Number of threads dedicated to concurrent bootstrap attempts. Defaults to 1.\nWarning: a larger amount of attempts may use additional system memory and disk IO.\ntype:uint64");
	toml.put ("bootstrap_serving_threads", bootstrap_serving_threads, "Number of threads dedicated to serving bootstrap data to other peers. Defaults to half the number of CPU threads, and at least 2.\ntype:uint64");
	toml.put ("bootstrap_frontier_request_count", bootstrap_frontier_request_count, "Number frontiers per bootstrap frontier request. Defaults to 1048576.\ntype:uint32,[1024..4294967295]");
	toml.put ("block_processor_batch_max_time", block_processor_batch_max_time.count (), "The maximum time the block processor can continuously process blocks for.\ntype:milliseconds");
	toml.put ("allow_local_peers", allow_local_peers, "Enable or disable local host peering.\ntype:bool");
	toml.put ("vote_minimum", vote_minimum.to_string_dec (), "Local representatives do not vote if the delegated weight is under this threshold. Saves on system resources.\ntype:string,amount,raw");
	toml.put ("vote_generator_delay", vote_generator_delay.count (), "Delay before votes are sent to allow for efficient bundling of hashes in votes.\ntype:milliseconds");
	toml.put ("vote_generator_threshold", vote_generator_threshold, "Number of bundled hashes required for an additional generator delay.\ntype:uint64,[1..11]");
	toml.put ("unchecked_cutoff_time", unchecked_cutoff_time.count (), "Number of seconds before deleting an unchecked entry.\nWarning: lower values (e.g., 3600 seconds, or 1 hour) may result in unsuccessful bootstraps, especially a bootstrap from scratch.\ntype:seconds");
	toml.put ("tcp_io_timeout", tcp_io_timeout.count (), "Timeout for TCP connect-, read- and write operations.\nWarning: a low value (e.g., below 5 seconds) may result in TCP connections failing.\ntype:seconds");
	toml.put ("pow_sleep_interval", pow_sleep_interval.count (), "Time to sleep between batch work generation attempts. Reduces max CPU usage at the expense of a longer generation time.\ntype:nanoseconds");
	toml.put ("external_address", external_address, "The external address of this node (NAT). If not set, the node will request this information via UPnP.\ntype:string,ip");
	toml.put ("external_port", external_port, "The external port number of this node (NAT). Only used if external_address is set.\ntype:uint16");
	toml.put ("tcp_incoming_connections_max", tcp_incoming_connections_max, "Maximum number of incoming TCP connections.\ntype:uint64");
	toml.put ("use_memory_pools", use_memory_pools, "If true, allocate memory from memory pools. Enabling this may improve performance. Memory is never released to the OS.\ntype:bool");
	toml.put ("confirmation_history_size", confirmation_history_size, "Maximum confirmation history size. If tracking the rate of block confirmations, the websocket feature is recommended instead.\ntype:uint64");
	toml.put ("active_elections_size", active_elections_size, "Number of active elections. Elections beyond this limit have limited survival time.\nWarning: modifying this value may result in a lower confirmation rate.\ntype:uint64,[250..]");

	toml.put ("bandwidth_limit", bandwidth_limit, "Outbound traffic limit in bytes/sec after which messages will be dropped.\nNote: changing to unlimited bandwidth (0) is not recommended for limited connections.\ntype:uint64");
	toml.put ("bandwidth_limit_burst_ratio", bandwidth_limit_burst_ratio, "Burst ratio for outbound traffic shaping.\ntype:double");

	toml.put ("bootstrap_bandwidth_limit", bootstrap_bandwidth_limit, "Outbound bootstrap traffic limit in bytes/sec after which messages will be dropped.\nNote: changing to unlimited bandwidth (0) is not recommended for limited connections.\ntype:uint64");
	toml.put ("bootstrap_bandwidth_burst_ratio", bootstrap_bandwidth_burst_ratio, "Burst ratio for outbound bootstrap traffic.\ntype:double");

	toml.put ("conf_height_processor_batch_min_time", conf_height_processor_batch_min_time.count (), "Minimum write batching time when there are blocks pending confirmation height.\ntype:milliseconds");
	toml.put ("backup_before_upgrade", backup_before_upgrade, "Backup the ledger database before performing upgrades.\nWarning: uses more disk storage and increases startup time when upgrading.\ntype:bool");
	toml.put ("max_work_generate_multiplier", max_work_generate_multiplier, "Maximum allowed difficulty multiplier for work generation.\ntype:double,[1..]");
	toml.put ("frontiers_confirmation", serialize_frontiers_confirmation (frontiers_confirmation), "Mode controlling frontier confirmation rate.\ntype:string,{auto,always,disabled}");
	toml.put ("max_queued_requests", max_queued_requests, "Limit for number of queued confirmation requests for one channel, after which new requests are dropped until the queue drops below this value.\ntype:uint32");
	toml.put ("rep_crawler_weight_minimum", rep_crawler_weight_minimum.to_string_dec (), "Rep crawler minimum weight, if this is less than minimum principal weight then this is taken as the minimum weight a rep must have to be tracked. If you want to track all reps set this to 0. If you do not want this to influence anything then set it to max value. This is only useful for debugging or for people who really know what they are doing.\ntype:string,amount,raw");
	toml.put ("backlog_scan_batch_size", backlog_scan_batch_size, "Number of accounts per second to process when doing backlog population scan. Increasing this value will help unconfirmed frontiers get into election prioritization queue faster, however it will also increase resource usage. \ntype:uint");
	toml.put ("backlog_scan_frequency", backlog_scan_frequency, "Backlog scan divides the scan into smaller batches, number of which is controlled by this value. Higher frequency helps to utilize resources more uniformly, however it also introduces more overhead. The resulting number of accounts per single batch is `backlog_scan_batch_size / backlog_scan_frequency` \ntype:uint");

	auto work_peers_l (toml.create_array ("work_peers", "A list of \"address:port\" entries to identify work peers."));
	for (auto i (work_peers.begin ()), n (work_peers.end ()); i != n; ++i)
	{
		work_peers_l->push_back (boost::str (boost::format ("%1%:%2%") % i->first % i->second));
	}

	auto preconfigured_peers_l (toml.create_array ("preconfigured_peers", "A list of \"address\" (hostname or ipv6 notation ip address) entries to identify preconfigured peers."));
	for (auto i (preconfigured_peers.begin ()), n (preconfigured_peers.end ()); i != n; ++i)
	{
		preconfigured_peers_l->push_back (*i);
	}

	auto preconfigured_representatives_l (toml.create_array ("preconfigured_representatives", "A list of representative account addresses used when creating new accounts in internal wallets."));
	for (auto i (preconfigured_representatives.begin ()), n (preconfigured_representatives.end ()); i != n; ++i)
	{
		preconfigured_representatives_l->push_back (i->to_account ());
	}

	/** Experimental node entries */
	nano::tomlconfig experimental_l;
	auto secondary_work_peers_l (experimental_l.create_array ("secondary_work_peers", "A list of \"address:port\" entries to identify work peers for secondary work generation."));
	for (auto i (secondary_work_peers.begin ()), n (secondary_work_peers.end ()); i != n; ++i)
	{
		secondary_work_peers_l->push_back (boost::str (boost::format ("%1%:%2%") % i->first % i->second));
	}
	experimental_l.put ("max_pruning_age", max_pruning_age.count (), "Time limit for blocks age after pruning.\ntype:seconds");
	experimental_l.put ("max_pruning_depth", max_pruning_depth, "Limit for full blocks in chain after pruning.\ntype:uint64");
	toml.put_child ("experimental", experimental_l);

	nano::tomlconfig callback_l;
	callback_l.put ("address", callback_address, "Callback address.\ntype:string,ip");
	callback_l.put ("port", callback_port, "Callback port number.\ntype:uint16");
	callback_l.put ("target", callback_target, "Callback target path.\ntype:string,uri");
	toml.put_child ("httpcallback", callback_l);

	nano::tomlconfig logging_l;
	logging.serialize_toml (logging_l);
	toml.put_child ("logging", logging_l);

	nano::tomlconfig websocket_l;
	websocket_config.serialize_toml (websocket_l);
	toml.put_child ("websocket", websocket_l);

	nano::tomlconfig ipc_l;
	ipc_config.serialize_toml (ipc_l);
	toml.put_child ("ipc", ipc_l);

	nano::tomlconfig diagnostics_l;
	diagnostics_config.serialize_toml (diagnostics_l);
	toml.put_child ("diagnostics", diagnostics_l);

	nano::tomlconfig stat_l;
	stat_config.serialize_toml (stat_l);
	toml.put_child ("statistics", stat_l);

	nano::tomlconfig rocksdb_l;
	rocksdb_config.serialize_toml (rocksdb_l);
	toml.put_child ("rocksdb", rocksdb_l);

	nano::tomlconfig lmdb_l;
	lmdb_config.serialize_toml (lmdb_l);
	toml.put_child ("lmdb", lmdb_l);

	return toml.get_error ();
}

nano::error nano::node_config::deserialize_toml (nano::tomlconfig & toml)
{
	try
	{
		if (toml.has_key ("httpcallback"))
		{
			auto callback_l (toml.get_required_child ("httpcallback"));
			callback_l.get<std::string> ("address", callback_address);
			callback_l.get<uint16_t> ("port", callback_port);
			callback_l.get<std::string> ("target", callback_target);
		}

		if (toml.has_key ("logging"))
		{
			auto logging_l (toml.get_required_child ("logging"));
			logging.deserialize_toml (logging_l);
		}

		if (toml.has_key ("websocket"))
		{
			auto websocket_config_l (toml.get_required_child ("websocket"));
			websocket_config.deserialize_toml (websocket_config_l);
		}

		if (toml.has_key ("ipc"))
		{
			auto ipc_config_l (toml.get_required_child ("ipc"));
			ipc_config.deserialize_toml (ipc_config_l);
		}

		if (toml.has_key ("diagnostics"))
		{
			auto diagnostics_config_l (toml.get_required_child ("diagnostics"));
			diagnostics_config.deserialize_toml (diagnostics_config_l);
		}

		if (toml.has_key ("statistics"))
		{
			auto stat_config_l (toml.get_required_child ("statistics"));
			stat_config.deserialize_toml (stat_config_l);
		}

		if (toml.has_key ("rocksdb"))
		{
			auto rocksdb_config_l (toml.get_required_child ("rocksdb"));
			rocksdb_config.deserialize_toml (rocksdb_config_l);
		}

		if (toml.has_key ("work_peers"))
		{
			work_peers.clear ();
			toml.array_entries_required<std::string> ("work_peers", [this] (std::string const & entry_a) {
				this->deserialize_address (entry_a, this->work_peers);
			});
		}

		if (toml.has_key (preconfigured_peers_key))
		{
			preconfigured_peers.clear ();
			toml.array_entries_required<std::string> (preconfigured_peers_key, [this] (std::string entry) {
				preconfigured_peers.push_back (entry);
			});
		}

		if (toml.has_key ("preconfigured_representatives"))
		{
			preconfigured_representatives.clear ();
			toml.array_entries_required<std::string> ("preconfigured_representatives", [this, &toml] (std::string entry) {
				nano::account representative{};
				if (representative.decode_account (entry))
				{
					toml.get_error ().set ("Invalid representative account: " + entry);
				}
				preconfigured_representatives.push_back (representative);
			});
		}

		if (preconfigured_representatives.empty ())
		{
			toml.get_error ().set ("At least one representative account must be set");
		}

		auto receive_minimum_l (receive_minimum.to_string_dec ());
		if (toml.has_key ("receive_minimum"))
		{
			receive_minimum_l = toml.get<std::string> ("receive_minimum");
		}
		if (receive_minimum.decode_dec (receive_minimum_l))
		{
			toml.get_error ().set ("receive_minimum contains an invalid decimal amount");
		}

		auto online_weight_minimum_l (online_weight_minimum.to_string_dec ());
		if (toml.has_key ("online_weight_minimum"))
		{
			online_weight_minimum_l = toml.get<std::string> ("online_weight_minimum");
		}
		if (online_weight_minimum.decode_dec (online_weight_minimum_l))
		{
			toml.get_error ().set ("online_weight_minimum contains an invalid decimal amount");
		}

		auto vote_minimum_l (vote_minimum.to_string_dec ());
		if (toml.has_key ("vote_minimum"))
		{
			vote_minimum_l = toml.get<std::string> ("vote_minimum");
		}
		if (vote_minimum.decode_dec (vote_minimum_l))
		{
			toml.get_error ().set ("vote_minimum contains an invalid decimal amount");
		}

		auto delay_l = vote_generator_delay.count ();
		toml.get ("vote_generator_delay", delay_l);
		vote_generator_delay = std::chrono::milliseconds (delay_l);

		toml.get<unsigned> ("vote_generator_threshold", vote_generator_threshold);

		auto block_processor_batch_max_time_l = block_processor_batch_max_time.count ();
		toml.get ("block_processor_batch_max_time", block_processor_batch_max_time_l);
		block_processor_batch_max_time = std::chrono::milliseconds (block_processor_batch_max_time_l);

		auto unchecked_cutoff_time_l = static_cast<unsigned long> (unchecked_cutoff_time.count ());
		toml.get ("unchecked_cutoff_time", unchecked_cutoff_time_l);
		unchecked_cutoff_time = std::chrono::seconds (unchecked_cutoff_time_l);

		auto tcp_io_timeout_l = static_cast<unsigned long> (tcp_io_timeout.count ());
		toml.get ("tcp_io_timeout", tcp_io_timeout_l);
		tcp_io_timeout = std::chrono::seconds (tcp_io_timeout_l);

		if (toml.has_key ("peering_port"))
		{
			std::uint16_t peering_port_l{};
			toml.get_required<uint16_t> ("peering_port", peering_port_l);
			peering_port = peering_port_l;
		}

		toml.get<unsigned> ("bootstrap_fraction_numerator", bootstrap_fraction_numerator);
		toml.get<unsigned> ("election_hint_weight_percent", election_hint_weight_percent);
		toml.get<unsigned> ("password_fanout", password_fanout);
		toml.get<unsigned> ("io_threads", io_threads);
		toml.get<unsigned> ("work_threads", work_threads);
		toml.get<unsigned> ("network_threads", network_threads);
		toml.get<unsigned> ("bootstrap_connections", bootstrap_connections);
		toml.get<unsigned> ("bootstrap_connections_max", bootstrap_connections_max);
		toml.get<unsigned> ("bootstrap_initiator_threads", bootstrap_initiator_threads);
		toml.get<unsigned> ("bootstrap_serving_threads", bootstrap_serving_threads);
		toml.get<uint32_t> ("bootstrap_frontier_request_count", bootstrap_frontier_request_count);
		toml.get<bool> ("enable_voting", enable_voting);
		toml.get<bool> ("allow_local_peers", allow_local_peers);
		toml.get<unsigned> (signature_checker_threads_key, signature_checker_threads);

		if (toml.has_key ("lmdb"))
		{
			auto lmdb_config_l (toml.get_required_child ("lmdb"));
			lmdb_config.deserialize_toml (lmdb_config_l);
		}

		boost::asio::ip::address_v6 external_address_l;
		toml.get<boost::asio::ip::address_v6> ("external_address", external_address_l);
		external_address = external_address_l.to_string ();
		toml.get<uint16_t> ("external_port", external_port);
		toml.get<unsigned> ("tcp_incoming_connections_max", tcp_incoming_connections_max);

		auto pow_sleep_interval_l (pow_sleep_interval.count ());
		toml.get (pow_sleep_interval_key, pow_sleep_interval_l);
		pow_sleep_interval = std::chrono::nanoseconds (pow_sleep_interval_l);
		toml.get<bool> ("use_memory_pools", use_memory_pools);
		toml.get<std::size_t> ("confirmation_history_size", confirmation_history_size);
		toml.get<std::size_t> ("active_elections_size", active_elections_size);

		toml.get<std::size_t> ("bandwidth_limit", bandwidth_limit);
		toml.get<double> ("bandwidth_limit_burst_ratio", bandwidth_limit_burst_ratio);

		toml.get<std::size_t> ("bootstrap_bandwidth_limit", bootstrap_bandwidth_limit);
		toml.get<double> ("bootstrap_bandwidth_burst_ratio", bootstrap_bandwidth_burst_ratio);

		toml.get<bool> ("backup_before_upgrade", backup_before_upgrade);

		auto conf_height_processor_batch_min_time_l (conf_height_processor_batch_min_time.count ());
		toml.get ("conf_height_processor_batch_min_time", conf_height_processor_batch_min_time_l);
		conf_height_processor_batch_min_time = std::chrono::milliseconds (conf_height_processor_batch_min_time_l);

		toml.get<double> ("max_work_generate_multiplier", max_work_generate_multiplier);

		toml.get<uint32_t> ("max_queued_requests", max_queued_requests);

		auto rep_crawler_weight_minimum_l (rep_crawler_weight_minimum.to_string_dec ());
		if (toml.has_key ("rep_crawler_weight_minimum"))
		{
			rep_crawler_weight_minimum_l = toml.get<std::string> ("rep_crawler_weight_minimum");
		}
		if (rep_crawler_weight_minimum.decode_dec (rep_crawler_weight_minimum_l))
		{
			toml.get_error ().set ("rep_crawler_weight_minimum contains an invalid decimal amount");
		}

		if (toml.has_key ("frontiers_confirmation"))
		{
			auto frontiers_confirmation_l (toml.get<std::string> ("frontiers_confirmation"));
			frontiers_confirmation = deserialize_frontiers_confirmation (frontiers_confirmation_l);
		}

		toml.get<unsigned> ("backlog_scan_batch_size", backlog_scan_batch_size);
		toml.get<unsigned> ("backlog_scan_frequency", backlog_scan_frequency);

		if (toml.has_key ("experimental"))
		{
			auto experimental_config_l (toml.get_required_child ("experimental"));
			if (experimental_config_l.has_key ("secondary_work_peers"))
			{
				secondary_work_peers.clear ();
				experimental_config_l.array_entries_required<std::string> ("secondary_work_peers", [this] (std::string const & entry_a) {
					this->deserialize_address (entry_a, this->secondary_work_peers);
				});
			}
			auto max_pruning_age_l (max_pruning_age.count ());
			experimental_config_l.get ("max_pruning_age", max_pruning_age_l);
			max_pruning_age = std::chrono::seconds (max_pruning_age_l);
			experimental_config_l.get<uint64_t> ("max_pruning_depth", max_pruning_depth);
		}

		// Validate ranges
		if (election_hint_weight_percent < 5 || election_hint_weight_percent > 50)
		{
			toml.get_error ().set ("election_hint_weight_percent must be a number between 5 and 50");
		}
		if (password_fanout < 16 || password_fanout > 1024 * 1024)
		{
			toml.get_error ().set ("password_fanout must be a number between 16 and 1048576");
		}
		if (io_threads == 0)
		{
			toml.get_error ().set ("io_threads must be non-zero");
		}
		if (active_elections_size <= 250 && !network_params.network.is_dev_network ())
		{
			toml.get_error ().set ("active_elections_size must be greater than 250");
		}
		if (bandwidth_limit > std::numeric_limits<std::size_t>::max ())
		{
			toml.get_error ().set ("bandwidth_limit unbounded = 0, default = 10485760, max = 18446744073709551615");
		}
		if (vote_generator_threshold < 1 || vote_generator_threshold > 11)
		{
			toml.get_error ().set ("vote_generator_threshold must be a number between 1 and 11");
		}
		if (max_work_generate_multiplier < 1)
		{
			toml.get_error ().set ("max_work_generate_multiplier must be greater than or equal to 1");
		}
		if (frontiers_confirmation == nano::frontiers_confirmation_mode::invalid)
		{
			toml.get_error ().set ("frontiers_confirmation value is invalid (available: always, auto, disabled)");
		}
		if (block_processor_batch_max_time < network_params.node.process_confirmed_interval)
		{
			toml.get_error ().set ((boost::format ("block_processor_batch_max_time value must be equal or larger than %1%ms") % network_params.node.process_confirmed_interval.count ()).str ());
		}
		if (max_pruning_age < std::chrono::seconds (5 * 60) && !network_params.network.is_dev_network ())
		{
			toml.get_error ().set ("max_pruning_age must be greater than or equal to 5 minutes");
		}
		if (bootstrap_frontier_request_count < 1024)
		{
			toml.get_error ().set ("bootstrap_frontier_request_count must be greater than or equal to 1024");
		}
	}
	catch (std::runtime_error const & ex)
	{
		toml.get_error ().set (ex.what ());
	}

	return toml.get_error ();
}

std::string nano::node_config::serialize_frontiers_confirmation (nano::frontiers_confirmation_mode mode_a) const
{
	switch (mode_a)
	{
		case nano::frontiers_confirmation_mode::always:
			return "always";
		case nano::frontiers_confirmation_mode::automatic:
			return "auto";
		case nano::frontiers_confirmation_mode::disabled:
			return "disabled";
		default:
			return "auto";
	}
}

nano::frontiers_confirmation_mode nano::node_config::deserialize_frontiers_confirmation (std::string const & string_a)
{
	if (string_a == "always")
	{
		return nano::frontiers_confirmation_mode::always;
	}
	else if (string_a == "auto")
	{
		return nano::frontiers_confirmation_mode::automatic;
	}
	else if (string_a == "disabled")
	{
		return nano::frontiers_confirmation_mode::disabled;
	}
	else
	{
		return nano::frontiers_confirmation_mode::invalid;
	}
}

void nano::node_config::deserialize_address (std::string const & entry_a, std::vector<std::pair<std::string, uint16_t>> & container_a) const
{
	auto port_position (entry_a.rfind (':'));
	bool result = (port_position == -1);
	if (!result)
	{
		auto port_str (entry_a.substr (port_position + 1));
		uint16_t port;
		result |= parse_port (port_str, port);
		if (!result)
		{
			auto address (entry_a.substr (0, port_position));
			container_a.emplace_back (address, port);
		}
	}
}

nano::account nano::node_config::random_representative () const
{
	debug_assert (!preconfigured_representatives.empty ());
	std::size_t index (nano::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (preconfigured_representatives.size () - 1)));
	auto result (preconfigured_representatives[index]);
	return result;
}
