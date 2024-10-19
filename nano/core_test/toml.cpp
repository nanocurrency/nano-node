#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/secure/utility.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include <numeric>
#include <sstream>
#include <string>

using namespace std::chrono_literals;

/** Ensure only different values survive a toml diff */
TEST (toml, diff)
{
	nano::tomlconfig defaults, other;

	// Defaults
	std::stringstream ss;
	ss << R"toml(
	a = false
	b = false
	)toml";

	defaults.read (ss);

	// User file. The rpc section is the same and doesn't need to be emitted
	std::stringstream ss_override;
	ss_override << R"toml(
	a = true
	b = false
	)toml";

	other.read (ss_override);
	other.erase_default_values (defaults);

	ASSERT_TRUE (other.has_key ("a"));
	ASSERT_FALSE (other.has_key ("b"));
}

/** Diff on equal toml files leads to an empty result */
TEST (toml, diff_equal)
{
	nano::tomlconfig defaults, other;

	std::stringstream ss;
	ss << R"toml(
	[node]
	allow_local_peers = false
	)toml";

	defaults.read (ss);

	std::stringstream ss_override;
	ss_override << R"toml(
	[node]
	allow_local_peers = false
	)toml";

	other.read (ss_override);
	other.erase_default_values (defaults);
	ASSERT_TRUE (other.empty ());
}

TEST (toml, daemon_config_update_array)
{
	nano::tomlconfig t;
	std::filesystem::path data_path (".");
	nano::daemon_config c{ data_path, nano::dev::network_params };
	c.node.preconfigured_peers.push_back ("dev-peer.org");
	c.serialize_toml (t);
	c.deserialize_toml (t);
	ASSERT_EQ (c.node.preconfigured_peers[0], "dev-peer.org");
}

/** Empty rpc config file should match a default config object */
TEST (toml, rpc_config_deserialize_defaults)
{
	std::stringstream ss;

	// A config file with values that differs from devnet defaults
	ss << R"toml(
	[process]
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	nano::rpc_config conf{ nano::dev::network_params.network };
	nano::rpc_config defaults{ nano::dev::network_params.network };
	conf.deserialize_toml (t);

	ASSERT_FALSE (t.get_error ()) << t.get_error ().get_message ();

	ASSERT_EQ (conf.address, defaults.address);
	ASSERT_EQ (conf.enable_control, defaults.enable_control);
	ASSERT_EQ (conf.max_json_depth, defaults.max_json_depth);
	ASSERT_EQ (conf.max_request_size, defaults.max_request_size);
	ASSERT_EQ (conf.port, defaults.port);

	ASSERT_EQ (conf.rpc_process.io_threads, defaults.rpc_process.io_threads);
	ASSERT_EQ (conf.rpc_process.ipc_address, defaults.rpc_process.ipc_address);
	ASSERT_EQ (conf.rpc_process.ipc_port, defaults.rpc_process.ipc_port);
	ASSERT_EQ (conf.rpc_process.num_ipc_connections, defaults.rpc_process.num_ipc_connections);

	ASSERT_EQ (conf.rpc_logging.log_rpc, defaults.rpc_logging.log_rpc);
}

/** Empty config file should match a default config object */
TEST (toml, daemon_config_deserialize_defaults)
{
	std::stringstream ss;
	ss << R"toml(
	[node]
	[node.backlog_population]
	[node.bootstrap_ascending]
	[node.bootstrap_server]
	[node.block_processor]
	[node.diagnostics.txn_tracking]
	[node.httpcallback]
	[node.ipc.local]
	[node.ipc.tcp]
	[node.logging]
	[node.statistics.log]
	[node.statistics.sampling]
	[node.vote_processor]
	[node.websocket]
	[node.lmdb]
	[node.rocksdb]
	[opencl]
	[rpc]
	[rpc.child_process]
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	nano::daemon_config conf;
	nano::daemon_config defaults;
	conf.deserialize_toml (t);

	ASSERT_FALSE (t.get_error ()) << t.get_error ().get_message ();

	ASSERT_EQ (conf.opencl_enable, defaults.opencl_enable);
	ASSERT_EQ (conf.opencl.device, defaults.opencl.device);
	ASSERT_EQ (conf.opencl.platform, defaults.opencl.platform);
	ASSERT_EQ (conf.opencl.threads, defaults.opencl.threads);
	ASSERT_EQ (conf.rpc_enable, defaults.rpc_enable);
	ASSERT_EQ (conf.rpc.enable_sign_hash, defaults.rpc.enable_sign_hash);
	ASSERT_EQ (conf.rpc.child_process.enable, defaults.rpc.child_process.enable);
	ASSERT_EQ (conf.rpc.child_process.rpc_path, defaults.rpc.child_process.rpc_path);

	ASSERT_EQ (conf.node.active_elections.size, defaults.node.active_elections.size);
	ASSERT_EQ (conf.node.allow_local_peers, defaults.node.allow_local_peers);
	ASSERT_EQ (conf.node.backup_before_upgrade, defaults.node.backup_before_upgrade);
	ASSERT_EQ (conf.node.bandwidth_limit, defaults.node.bandwidth_limit);
	ASSERT_EQ (conf.node.bandwidth_limit_burst_ratio, defaults.node.bandwidth_limit_burst_ratio);
	ASSERT_EQ (conf.node.bootstrap_bandwidth_limit, defaults.node.bootstrap_bandwidth_limit);
	ASSERT_EQ (conf.node.bootstrap_bandwidth_burst_ratio, defaults.node.bootstrap_bandwidth_burst_ratio);
	ASSERT_EQ (conf.node.block_processor_batch_max_time, defaults.node.block_processor_batch_max_time);
	ASSERT_EQ (conf.node.bootstrap_connections, defaults.node.bootstrap_connections);
	ASSERT_EQ (conf.node.bootstrap_connections_max, defaults.node.bootstrap_connections_max);
	ASSERT_EQ (conf.node.bootstrap_initiator_threads, defaults.node.bootstrap_initiator_threads);
	ASSERT_EQ (conf.node.bootstrap_serving_threads, defaults.node.bootstrap_serving_threads);
	ASSERT_EQ (conf.node.bootstrap_frontier_request_count, defaults.node.bootstrap_frontier_request_count);
	ASSERT_EQ (conf.node.bootstrap_fraction_numerator, defaults.node.bootstrap_fraction_numerator);
	ASSERT_EQ (conf.node.confirming_set_batch_time, defaults.node.confirming_set_batch_time);
	ASSERT_EQ (conf.node.enable_voting, defaults.node.enable_voting);
	ASSERT_EQ (conf.node.external_address, defaults.node.external_address);
	ASSERT_EQ (conf.node.external_port, defaults.node.external_port);
	ASSERT_EQ (conf.node.io_threads, defaults.node.io_threads);
	ASSERT_EQ (conf.node.max_work_generate_multiplier, defaults.node.max_work_generate_multiplier);
	ASSERT_EQ (conf.node.network_threads, defaults.node.network_threads);
	ASSERT_EQ (conf.node.background_threads, defaults.node.background_threads);
	ASSERT_EQ (conf.node.secondary_work_peers, defaults.node.secondary_work_peers);
	ASSERT_EQ (conf.node.online_weight_minimum, defaults.node.online_weight_minimum);
	ASSERT_EQ (conf.node.representative_vote_weight_minimum, defaults.node.representative_vote_weight_minimum);
	ASSERT_EQ (conf.node.rep_crawler_weight_minimum, defaults.node.rep_crawler_weight_minimum);
	ASSERT_EQ (conf.node.password_fanout, defaults.node.password_fanout);
	ASSERT_EQ (conf.node.peering_port, defaults.node.peering_port);
	ASSERT_EQ (conf.node.pow_sleep_interval, defaults.node.pow_sleep_interval);
	ASSERT_EQ (conf.node.preconfigured_peers, defaults.node.preconfigured_peers);
	ASSERT_EQ (conf.node.preconfigured_representatives, defaults.node.preconfigured_representatives);
	ASSERT_EQ (conf.node.receive_minimum, defaults.node.receive_minimum);
	ASSERT_EQ (conf.node.signature_checker_threads, defaults.node.signature_checker_threads);
	ASSERT_EQ (conf.node.tcp_incoming_connections_max, defaults.node.tcp_incoming_connections_max);
	ASSERT_EQ (conf.node.tcp_io_timeout, defaults.node.tcp_io_timeout);
	ASSERT_EQ (conf.node.unchecked_cutoff_time, defaults.node.unchecked_cutoff_time);
	ASSERT_EQ (conf.node.use_memory_pools, defaults.node.use_memory_pools);
	ASSERT_EQ (conf.node.vote_generator_delay, defaults.node.vote_generator_delay);
	ASSERT_EQ (conf.node.vote_generator_threshold, defaults.node.vote_generator_threshold);
	ASSERT_EQ (conf.node.vote_minimum, defaults.node.vote_minimum);
	ASSERT_EQ (conf.node.work_peers, defaults.node.work_peers);
	ASSERT_EQ (conf.node.work_threads, defaults.node.work_threads);
	ASSERT_EQ (conf.node.max_queued_requests, defaults.node.max_queued_requests);
	ASSERT_EQ (conf.node.request_aggregator_threads, defaults.node.request_aggregator_threads);
	ASSERT_EQ (conf.node.max_unchecked_blocks, defaults.node.max_unchecked_blocks);
	ASSERT_EQ (conf.node.backlog_population.enable, defaults.node.backlog_population.enable);
	ASSERT_EQ (conf.node.backlog_population.batch_size, defaults.node.backlog_population.batch_size);
	ASSERT_EQ (conf.node.backlog_population.frequency, defaults.node.backlog_population.frequency);
	ASSERT_EQ (conf.node.enable_upnp, defaults.node.enable_upnp);

	ASSERT_EQ (conf.node.websocket_config.enabled, defaults.node.websocket_config.enabled);
	ASSERT_EQ (conf.node.websocket_config.address, defaults.node.websocket_config.address);
	ASSERT_EQ (conf.node.websocket_config.port, defaults.node.websocket_config.port);

	ASSERT_EQ (conf.node.callback_address, defaults.node.callback_address);
	ASSERT_EQ (conf.node.callback_port, defaults.node.callback_port);
	ASSERT_EQ (conf.node.callback_target, defaults.node.callback_target);

	ASSERT_EQ (conf.node.ipc_config.transport_domain.allow_unsafe, defaults.node.ipc_config.transport_domain.allow_unsafe);
	ASSERT_EQ (conf.node.ipc_config.transport_domain.enabled, defaults.node.ipc_config.transport_domain.enabled);
	ASSERT_EQ (conf.node.ipc_config.transport_domain.io_timeout, defaults.node.ipc_config.transport_domain.io_timeout);
	ASSERT_EQ (conf.node.ipc_config.transport_domain.io_threads, defaults.node.ipc_config.transport_domain.io_threads);
	ASSERT_EQ (conf.node.ipc_config.transport_domain.path, defaults.node.ipc_config.transport_domain.path);
	ASSERT_EQ (conf.node.ipc_config.transport_tcp.enabled, defaults.node.ipc_config.transport_tcp.enabled);
	ASSERT_EQ (conf.node.ipc_config.transport_tcp.io_timeout, defaults.node.ipc_config.transport_tcp.io_timeout);
	ASSERT_EQ (conf.node.ipc_config.transport_tcp.io_threads, defaults.node.ipc_config.transport_tcp.io_threads);
	ASSERT_EQ (conf.node.ipc_config.transport_tcp.port, defaults.node.ipc_config.transport_tcp.port);
	ASSERT_EQ (conf.node.ipc_config.flatbuffers.skip_unexpected_fields_in_json, defaults.node.ipc_config.flatbuffers.skip_unexpected_fields_in_json);
	ASSERT_EQ (conf.node.ipc_config.flatbuffers.verify_buffers, defaults.node.ipc_config.flatbuffers.verify_buffers);

	ASSERT_EQ (conf.node.diagnostics_config.txn_tracking.enable, defaults.node.diagnostics_config.txn_tracking.enable);
	ASSERT_EQ (conf.node.diagnostics_config.txn_tracking.ignore_writes_below_block_processor_max_time, defaults.node.diagnostics_config.txn_tracking.ignore_writes_below_block_processor_max_time);
	ASSERT_EQ (conf.node.diagnostics_config.txn_tracking.min_read_txn_time, defaults.node.diagnostics_config.txn_tracking.min_read_txn_time);
	ASSERT_EQ (conf.node.diagnostics_config.txn_tracking.min_write_txn_time, defaults.node.diagnostics_config.txn_tracking.min_write_txn_time);

	ASSERT_EQ (conf.node.stats_config.max_samples, defaults.node.stats_config.max_samples);
	ASSERT_EQ (conf.node.stats_config.log_rotation_count, defaults.node.stats_config.log_rotation_count);
	ASSERT_EQ (conf.node.stats_config.log_samples_interval, defaults.node.stats_config.log_samples_interval);
	ASSERT_EQ (conf.node.stats_config.log_counters_interval, defaults.node.stats_config.log_counters_interval);
	ASSERT_EQ (conf.node.stats_config.log_headers, defaults.node.stats_config.log_headers);
	ASSERT_EQ (conf.node.stats_config.log_counters_filename, defaults.node.stats_config.log_counters_filename);
	ASSERT_EQ (conf.node.stats_config.log_samples_filename, defaults.node.stats_config.log_samples_filename);

	ASSERT_EQ (conf.node.lmdb_config.sync, defaults.node.lmdb_config.sync);
	ASSERT_EQ (conf.node.lmdb_config.max_databases, defaults.node.lmdb_config.max_databases);
	ASSERT_EQ (conf.node.lmdb_config.map_size, defaults.node.lmdb_config.map_size);

	ASSERT_EQ (conf.node.rocksdb_config.enable, defaults.node.rocksdb_config.enable);
	ASSERT_EQ (conf.node.rocksdb_config.io_threads, defaults.node.rocksdb_config.io_threads);
	ASSERT_EQ (conf.node.rocksdb_config.read_cache, defaults.node.rocksdb_config.read_cache);
	ASSERT_EQ (conf.node.rocksdb_config.write_cache, defaults.node.rocksdb_config.write_cache);

	ASSERT_EQ (conf.node.optimistic_scheduler.enable, defaults.node.optimistic_scheduler.enable);
	ASSERT_EQ (conf.node.optimistic_scheduler.gap_threshold, defaults.node.optimistic_scheduler.gap_threshold);
	ASSERT_EQ (conf.node.optimistic_scheduler.max_size, defaults.node.optimistic_scheduler.max_size);

	ASSERT_EQ (conf.node.hinted_scheduler.enable, defaults.node.hinted_scheduler.enable);
	ASSERT_EQ (conf.node.hinted_scheduler.hinting_threshold_percent, defaults.node.hinted_scheduler.hinting_threshold_percent);
	ASSERT_EQ (conf.node.hinted_scheduler.check_interval.count (), defaults.node.hinted_scheduler.check_interval.count ());
	ASSERT_EQ (conf.node.hinted_scheduler.block_cooldown.count (), defaults.node.hinted_scheduler.block_cooldown.count ());
	ASSERT_EQ (conf.node.hinted_scheduler.vacancy_threshold_percent, defaults.node.hinted_scheduler.vacancy_threshold_percent);

	ASSERT_EQ (conf.node.vote_cache.max_size, defaults.node.vote_cache.max_size);
	ASSERT_EQ (conf.node.vote_cache.max_voters, defaults.node.vote_cache.max_voters);

	ASSERT_EQ (conf.node.block_processor.max_peer_queue, defaults.node.block_processor.max_peer_queue);
	ASSERT_EQ (conf.node.block_processor.max_system_queue, defaults.node.block_processor.max_system_queue);
	ASSERT_EQ (conf.node.block_processor.priority_live, defaults.node.block_processor.priority_live);
	ASSERT_EQ (conf.node.block_processor.priority_bootstrap, defaults.node.block_processor.priority_bootstrap);
	ASSERT_EQ (conf.node.block_processor.priority_local, defaults.node.block_processor.priority_local);

	ASSERT_EQ (conf.node.vote_processor.max_pr_queue, defaults.node.vote_processor.max_pr_queue);
	ASSERT_EQ (conf.node.vote_processor.max_non_pr_queue, defaults.node.vote_processor.max_non_pr_queue);
	ASSERT_EQ (conf.node.vote_processor.pr_priority, defaults.node.vote_processor.pr_priority);
	ASSERT_EQ (conf.node.vote_processor.threads, defaults.node.vote_processor.threads);
	ASSERT_EQ (conf.node.vote_processor.batch_size, defaults.node.vote_processor.batch_size);

	ASSERT_EQ (conf.node.bootstrap_ascending.enable, defaults.node.bootstrap_ascending.enable);
	ASSERT_EQ (conf.node.bootstrap_ascending.enable_database_scan, defaults.node.bootstrap_ascending.enable_database_scan);
	ASSERT_EQ (conf.node.bootstrap_ascending.enable_dependency_walker, defaults.node.bootstrap_ascending.enable_dependency_walker);
	ASSERT_EQ (conf.node.bootstrap_ascending.channel_limit, defaults.node.bootstrap_ascending.channel_limit);
	ASSERT_EQ (conf.node.bootstrap_ascending.database_rate_limit, defaults.node.bootstrap_ascending.database_rate_limit);
	ASSERT_EQ (conf.node.bootstrap_ascending.database_warmup_ratio, defaults.node.bootstrap_ascending.database_warmup_ratio);
	ASSERT_EQ (conf.node.bootstrap_ascending.max_pull_count, defaults.node.bootstrap_ascending.max_pull_count);
	ASSERT_EQ (conf.node.bootstrap_ascending.request_timeout, defaults.node.bootstrap_ascending.request_timeout);
	ASSERT_EQ (conf.node.bootstrap_ascending.throttle_coefficient, defaults.node.bootstrap_ascending.throttle_coefficient);
	ASSERT_EQ (conf.node.bootstrap_ascending.throttle_wait, defaults.node.bootstrap_ascending.throttle_wait);
	ASSERT_EQ (conf.node.bootstrap_ascending.block_processor_threshold, defaults.node.bootstrap_ascending.block_processor_threshold);
	ASSERT_EQ (conf.node.bootstrap_ascending.max_requests, defaults.node.bootstrap_ascending.max_requests);

	ASSERT_EQ (conf.node.bootstrap_server.max_queue, defaults.node.bootstrap_server.max_queue);
	ASSERT_EQ (conf.node.bootstrap_server.threads, defaults.node.bootstrap_server.threads);
	ASSERT_EQ (conf.node.bootstrap_server.batch_size, defaults.node.bootstrap_server.batch_size);

	ASSERT_EQ (conf.node.request_aggregator.max_queue, defaults.node.request_aggregator.max_queue);
	ASSERT_EQ (conf.node.request_aggregator.threads, defaults.node.request_aggregator.threads);
	ASSERT_EQ (conf.node.request_aggregator.batch_size, defaults.node.request_aggregator.batch_size);

	ASSERT_EQ (conf.node.message_processor.threads, defaults.node.message_processor.threads);
	ASSERT_EQ (conf.node.message_processor.max_queue, defaults.node.message_processor.max_queue);
}

TEST (toml, optional_child)
{
	std::stringstream ss;
	ss << R"toml(
		[child]
		val=1
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	auto c1 = t.get_required_child ("child");
	int val = 0;
	c1.get_required ("val", val);
	ASSERT_EQ (val, 1);
	auto c2 = t.get_optional_child ("child2");
	ASSERT_FALSE (c2);
}

/** Config settings passed via CLI overrides the config file settings. This is solved
using an override stream. */
TEST (toml, dot_child_syntax)
{
	std::stringstream ss_override;
	ss_override << R"toml(
		node.a = 1
		node.b = 2
	)toml";

	std::stringstream ss;
	ss << R"toml(
		[node]
		b=5
		c=3
	)toml";

	nano::tomlconfig t;
	t.read (ss_override, ss);

	auto node = t.get_required_child ("node");
	uint16_t a, b, c;
	node.get<uint16_t> ("a", a);
	ASSERT_EQ (a, 1);
	node.get<uint16_t> ("b", b);
	ASSERT_EQ (b, 2);
	node.get<uint16_t> ("c", c);
	ASSERT_EQ (c, 3);
}

TEST (toml, base_override)
{
	std::stringstream ss_base;
	ss_base << R"toml(
	        node.peering_port=7075
	)toml";

	std::stringstream ss_override;
	ss_override << R"toml(
	        node.peering_port=8075
			node.too_big=70000
	)toml";

	nano::tomlconfig t;
	t.read (ss_override, ss_base);

	// Query optional existent value
	uint16_t port = 0;
	t.get_optional<uint16_t> ("node.peering_port", port);
	ASSERT_EQ (port, 8075);
	ASSERT_FALSE (t.get_error ());

	// Query optional non-existent value, make sure we get default and no errors
	port = 65535;
	t.get_optional<uint16_t> ("node.peering_port_non_existent", port);
	ASSERT_EQ (port, 65535);
	ASSERT_FALSE (t.get_error ());
	t.get_error ().clear ();

	// Query required non-existent value, make sure it errors
	t.get_required<uint16_t> ("node.peering_port_not_existent", port);
	ASSERT_EQ (port, 65535);
	ASSERT_TRUE (t.get_error ());
	ASSERT_EQ (t.get_error (), nano::error_config::missing_value);
	t.get_error ().clear ();

	// Query uint16 that's too big, make sure we have an error
	t.get_required<uint16_t> ("node.too_big", port);
	ASSERT_TRUE (t.get_error ());
	ASSERT_EQ (t.get_error (), nano::error_config::invalid_value);
}

TEST (toml, put)
{
	nano::tomlconfig config;
	nano::tomlconfig config_node;
	// Overwrite value and add to child node
	config_node.put ("port", "7074");
	config_node.put ("port", "7075");
	config.put_child ("node", config_node);
	uint16_t port;
	config.get_required<uint16_t> ("node.port", port);
	ASSERT_EQ (port, 7075);
	ASSERT_FALSE (config.get_error ());
}

TEST (toml, array)
{
	nano::tomlconfig config;
	nano::tomlconfig config_node;
	config.put_child ("node", config_node);
	config_node.push<std::string> ("items", "item 1");
	config_node.push<std::string> ("items", "item 2");
	int i = 1;
	config_node.array_entries_required<std::string> ("items", [&i] (std::string item) {
		ASSERT_EQ (item, std::string ("item ") + std::to_string (i));
		i++;
	});
}

/** Deserialize a node config with non-default values */
TEST (toml, daemon_config_deserialize_no_defaults)
{
	std::stringstream ss;

	ss << R"toml(
	[node]
	allow_local_peers = false
	backup_before_upgrade = true
	bandwidth_limit = 999
	bandwidth_limit_burst_ratio = 999.9
	bootstrap_bandwidth_limit = 999
	bootstrap_bandwidth_burst_ratio = 999.9
	block_processor_batch_max_time = 999
	bootstrap_connections = 999
	bootstrap_connections_max = 999
	bootstrap_initiator_threads = 999
	bootstrap_serving_threads = 999
	bootstrap_frontier_request_count = 9999
	bootstrap_fraction_numerator = 999
	confirming_set_batch_time = 999
	enable_voting = false
	external_address = "0:0:0:0:0:ffff:7f01:101"
	external_port = 999
	io_threads = 999
	lmdb_max_dbs = 999
	network_threads = 999
	background_threads = 999
	online_weight_minimum = "999"
	representative_vote_weight_minimum = "999"
	rep_crawler_weight_minimum = "999"
	password_fanout = 999
	peering_port = 999
	pow_sleep_interval= 999
	preconfigured_peers = ["dev.org"]
	preconfigured_representatives = ["nano_3arg3asgtigae3xckabaaewkx3bzsh7nwz7jkmjos79ihyaxwphhm6qgjps4"]
	receive_minimum = "999"
	signature_checker_threads = 999
	tcp_incoming_connections_max = 999
	tcp_io_timeout = 999
	unchecked_cutoff_time = 999
	use_memory_pools = false
	vote_generator_delay = 999
	vote_generator_threshold = 9
	vote_minimum = "999"
	work_peers = ["dev.org:999"]
	work_threads = 999
	max_work_generate_multiplier = 1.0
	max_queued_requests = 999
	request_aggregator_threads = 999
	max_unchecked_blocks = 999
	frontiers_confirmation = "always"
	enable_upnp = false

	[node.backlog_population]
	enable = false
	batch_size = 999
	frequency = 999

	[node.block_processor]
	max_peer_queue = 999
	max_system_queue = 999
	priority_live = 999
	priority_bootstrap = 999
	priority_local = 999

	[node.active_elections]
	size = 999
	hinted_limit_percentage = 90
	optimistic_limit_percentage = 90
	confirmation_history_size = 999
	confirmation_cache = 999

	[node.diagnostics.txn_tracking]
	enable = true
	ignore_writes_below_block_processor_max_time = false
	min_read_txn_time = 999
	min_write_txn_time = 999

	[node.httpcallback]
	address = "dev.org"
	port = 999
	target = "/dev"

	[node.ipc.local]
	allow_unsafe = true
	enable = true
	io_timeout = 999
	io_threads = 999
	path = "/tmp/dev"

	[node.ipc.tcp]
	enable = true
	io_timeout = 999
	io_threads = 999
	port = 999

	[node.ipc.flatbuffers]
	skip_unexpected_fields_in_json = false
	verify_buffers = false

	[node.logging]
	bulk_pull = true
	flush = false
	insufficient_work = false
	ledger = true
	ledger_duplicate = true
	log_ipc = false
	log_to_cerr = true
	max_size = 999
	min_time_between_output = 999
	network = false
	network_keepalive = true
	network_message = true
	network_node_id_handshake = true
	network_telemetry_logging = true
	network_rejected_logging = true
	network_packet = true
	network_publish = true
	network_timeout = true
	node_lifetime_tracing = true
	rotation_size = 999
	single_line_record = true
	stable_log_filename = true
	timing = true
	active_update = true
	upnp_details = true
	vote = true
	rep_crawler = true
	work_generation_time = false

	[node.statistics]
	max_samples = 999

	[node.statistics.log]
	filename_counters = "devcounters.stat"
	filename_samples = "devsamples.stat"
	headers = false
	interval_counters = 999
	interval_samples = 999
	rotation_count = 999

	[node.websocket]
	address = "0:0:0:0:0:ffff:7f01:101"
	enable = true
	port = 999

	[node.lmdb]
	sync = "nosync_safe"
	max_databases = 999
	map_size = 999

	[node.optimistic_scheduler]
	enable = false
	gap_threshold = 999
	max_size = 999

	[node.hinted_scheduler]
	enable = false
	hinting_threshold = 99
	check_interval = 999
	block_cooldown = 999
	vacancy_threshold = 99

	[node.rocksdb]
	enable = true
	io_threads = 99
	read_cache = 99
	write_cache = 99

	[node.experimental]
	secondary_work_peers = ["dev.org:998"]
	max_pruning_age = 999
	max_pruning_depth = 999

	[node.vote_cache]
	max_size = 999
	max_voters = 999

	[node.vote_processor]
	max_pr_queue = 999
	max_non_pr_queue = 999
	pr_priority = 999
	threads = 999
	batch_size = 999

	[node.bootstrap_ascending]
	enable = false
	enable_frontier_scan = false
	enable_database_scan = true
	enable_dependency_walker = false
	channel_limit = 999
	database_rate_limit = 999
	database_warmup_ratio = 999
	max_pull_count = 999
	request_timeout = 999
	throttle_coefficient = 999
	throttle_wait = 999
	block_processor_threshold = 999
	max_requests = 999

	[node.bootstrap_server]
	max_queue = 999
	threads = 999
	batch_size = 999

	[node.request_aggregator]
	max_queue = 999
	threads = 999
	batch_size = 999

	[node.message_processor]
	threads = 999
	max_queue = 999

	[opencl]
	device = 999
	enable = true
	platform = 999
	threads = 999

	[rpc]
	enable = true
	enable_sign_hash = true

	[rpc.child_process]
	enable = true
	rpc_path = "/dev/nano_rpc"
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);
	nano::daemon_config conf;
	nano::daemon_config defaults;
	conf.deserialize_toml (toml);

	ASSERT_FALSE (toml.get_error ()) << toml.get_error ().get_message ();

	ASSERT_NE (conf.opencl_enable, defaults.opencl_enable);
	ASSERT_NE (conf.opencl.device, defaults.opencl.device);
	ASSERT_NE (conf.opencl.platform, defaults.opencl.platform);
	ASSERT_NE (conf.opencl.threads, defaults.opencl.threads);
	ASSERT_NE (conf.rpc_enable, defaults.rpc_enable);
	ASSERT_NE (conf.rpc.enable_sign_hash, defaults.rpc.enable_sign_hash);
	ASSERT_NE (conf.rpc.child_process.enable, defaults.rpc.child_process.enable);
	ASSERT_NE (conf.rpc.child_process.rpc_path, defaults.rpc.child_process.rpc_path);

	ASSERT_NE (conf.node.active_elections.size, defaults.node.active_elections.size);
	ASSERT_NE (conf.node.allow_local_peers, defaults.node.allow_local_peers);
	ASSERT_NE (conf.node.backup_before_upgrade, defaults.node.backup_before_upgrade);
	ASSERT_NE (conf.node.bandwidth_limit, defaults.node.bandwidth_limit);
	ASSERT_NE (conf.node.bandwidth_limit_burst_ratio, defaults.node.bandwidth_limit_burst_ratio);
	ASSERT_NE (conf.node.bootstrap_bandwidth_limit, defaults.node.bootstrap_bandwidth_limit);
	ASSERT_NE (conf.node.bootstrap_bandwidth_burst_ratio, defaults.node.bootstrap_bandwidth_burst_ratio);
	ASSERT_NE (conf.node.block_processor_batch_max_time, defaults.node.block_processor_batch_max_time);
	ASSERT_NE (conf.node.bootstrap_connections, defaults.node.bootstrap_connections);
	ASSERT_NE (conf.node.bootstrap_connections_max, defaults.node.bootstrap_connections_max);
	ASSERT_NE (conf.node.bootstrap_initiator_threads, defaults.node.bootstrap_initiator_threads);
	ASSERT_NE (conf.node.bootstrap_serving_threads, defaults.node.bootstrap_serving_threads);
	ASSERT_NE (conf.node.bootstrap_frontier_request_count, defaults.node.bootstrap_frontier_request_count);
	ASSERT_NE (conf.node.bootstrap_fraction_numerator, defaults.node.bootstrap_fraction_numerator);
	ASSERT_NE (conf.node.confirming_set_batch_time, defaults.node.confirming_set_batch_time);
	ASSERT_NE (conf.node.enable_voting, defaults.node.enable_voting);
	ASSERT_NE (conf.node.external_address, defaults.node.external_address);
	ASSERT_NE (conf.node.external_port, defaults.node.external_port);
	ASSERT_NE (conf.node.io_threads, defaults.node.io_threads);
	ASSERT_NE (conf.node.max_work_generate_multiplier, defaults.node.max_work_generate_multiplier);
	ASSERT_NE (conf.node.max_unchecked_blocks, defaults.node.max_unchecked_blocks);
	ASSERT_NE (conf.node.network_threads, defaults.node.network_threads);
	ASSERT_NE (conf.node.background_threads, defaults.node.background_threads);
	ASSERT_NE (conf.node.secondary_work_peers, defaults.node.secondary_work_peers);
	ASSERT_NE (conf.node.max_pruning_age, defaults.node.max_pruning_age);
	ASSERT_NE (conf.node.max_pruning_depth, defaults.node.max_pruning_depth);
	ASSERT_NE (conf.node.online_weight_minimum, defaults.node.online_weight_minimum);
	ASSERT_NE (conf.node.representative_vote_weight_minimum, defaults.node.representative_vote_weight_minimum);
	ASSERT_NE (conf.node.rep_crawler_weight_minimum, defaults.node.rep_crawler_weight_minimum);
	ASSERT_NE (conf.node.password_fanout, defaults.node.password_fanout);
	ASSERT_NE (conf.node.peering_port, defaults.node.peering_port);
	ASSERT_NE (conf.node.pow_sleep_interval, defaults.node.pow_sleep_interval);
	ASSERT_NE (conf.node.preconfigured_peers, defaults.node.preconfigured_peers);
	ASSERT_NE (conf.node.preconfigured_representatives, defaults.node.preconfigured_representatives);
	ASSERT_NE (conf.node.receive_minimum, defaults.node.receive_minimum);
	ASSERT_NE (conf.node.signature_checker_threads, defaults.node.signature_checker_threads);
	ASSERT_NE (conf.node.tcp_incoming_connections_max, defaults.node.tcp_incoming_connections_max);
	ASSERT_NE (conf.node.tcp_io_timeout, defaults.node.tcp_io_timeout);
	ASSERT_NE (conf.node.unchecked_cutoff_time, defaults.node.unchecked_cutoff_time);
	ASSERT_NE (conf.node.use_memory_pools, defaults.node.use_memory_pools);
	ASSERT_NE (conf.node.vote_generator_delay, defaults.node.vote_generator_delay);
	ASSERT_NE (conf.node.vote_generator_threshold, defaults.node.vote_generator_threshold);
	ASSERT_NE (conf.node.vote_minimum, defaults.node.vote_minimum);
	ASSERT_NE (conf.node.work_peers, defaults.node.work_peers);
	ASSERT_NE (conf.node.work_threads, defaults.node.work_threads);
	ASSERT_NE (conf.node.max_queued_requests, defaults.node.max_queued_requests);
	ASSERT_NE (conf.node.request_aggregator_threads, defaults.node.request_aggregator_threads);
	ASSERT_NE (conf.node.backlog_population.enable, defaults.node.backlog_population.enable);
	ASSERT_NE (conf.node.backlog_population.batch_size, defaults.node.backlog_population.batch_size);
	ASSERT_NE (conf.node.backlog_population.frequency, defaults.node.backlog_population.frequency);
	ASSERT_NE (conf.node.enable_upnp, defaults.node.enable_upnp);

	ASSERT_NE (conf.node.websocket_config.enabled, defaults.node.websocket_config.enabled);
	ASSERT_NE (conf.node.websocket_config.address, defaults.node.websocket_config.address);
	ASSERT_NE (conf.node.websocket_config.port, defaults.node.websocket_config.port);

	ASSERT_NE (conf.node.callback_address, defaults.node.callback_address);
	ASSERT_NE (conf.node.callback_port, defaults.node.callback_port);
	ASSERT_NE (conf.node.callback_target, defaults.node.callback_target);

	ASSERT_NE (conf.node.ipc_config.transport_domain.allow_unsafe, defaults.node.ipc_config.transport_domain.allow_unsafe);
	ASSERT_NE (conf.node.ipc_config.transport_domain.enabled, defaults.node.ipc_config.transport_domain.enabled);
	ASSERT_NE (conf.node.ipc_config.transport_domain.io_timeout, defaults.node.ipc_config.transport_domain.io_timeout);
	ASSERT_NE (conf.node.ipc_config.transport_domain.io_threads, defaults.node.ipc_config.transport_domain.io_threads);
	ASSERT_NE (conf.node.ipc_config.transport_domain.path, defaults.node.ipc_config.transport_domain.path);
	ASSERT_NE (conf.node.ipc_config.transport_tcp.enabled, defaults.node.ipc_config.transport_tcp.enabled);
	ASSERT_NE (conf.node.ipc_config.transport_tcp.io_timeout, defaults.node.ipc_config.transport_tcp.io_timeout);
	ASSERT_NE (conf.node.ipc_config.transport_tcp.io_threads, defaults.node.ipc_config.transport_tcp.io_threads);
	ASSERT_NE (conf.node.ipc_config.transport_tcp.port, defaults.node.ipc_config.transport_tcp.port);
	ASSERT_NE (conf.node.ipc_config.flatbuffers.skip_unexpected_fields_in_json, defaults.node.ipc_config.flatbuffers.skip_unexpected_fields_in_json);
	ASSERT_NE (conf.node.ipc_config.flatbuffers.verify_buffers, defaults.node.ipc_config.flatbuffers.verify_buffers);

	ASSERT_NE (conf.node.diagnostics_config.txn_tracking.enable, defaults.node.diagnostics_config.txn_tracking.enable);
	ASSERT_NE (conf.node.diagnostics_config.txn_tracking.ignore_writes_below_block_processor_max_time, defaults.node.diagnostics_config.txn_tracking.ignore_writes_below_block_processor_max_time);
	ASSERT_NE (conf.node.diagnostics_config.txn_tracking.min_read_txn_time, defaults.node.diagnostics_config.txn_tracking.min_read_txn_time);
	ASSERT_NE (conf.node.diagnostics_config.txn_tracking.min_write_txn_time, defaults.node.diagnostics_config.txn_tracking.min_write_txn_time);

	ASSERT_NE (conf.node.stats_config.max_samples, defaults.node.stats_config.max_samples);
	ASSERT_NE (conf.node.stats_config.log_rotation_count, defaults.node.stats_config.log_rotation_count);
	ASSERT_NE (conf.node.stats_config.log_samples_interval, defaults.node.stats_config.log_samples_interval);
	ASSERT_NE (conf.node.stats_config.log_counters_interval, defaults.node.stats_config.log_counters_interval);
	ASSERT_NE (conf.node.stats_config.log_headers, defaults.node.stats_config.log_headers);
	ASSERT_NE (conf.node.stats_config.log_counters_filename, defaults.node.stats_config.log_counters_filename);
	ASSERT_NE (conf.node.stats_config.log_samples_filename, defaults.node.stats_config.log_samples_filename);

	ASSERT_NE (conf.node.lmdb_config.sync, defaults.node.lmdb_config.sync);
	ASSERT_NE (conf.node.lmdb_config.max_databases, defaults.node.lmdb_config.max_databases);
	ASSERT_NE (conf.node.lmdb_config.map_size, defaults.node.lmdb_config.map_size);

	ASSERT_TRUE (conf.node.rocksdb_config.enable);
	ASSERT_EQ (nano::rocksdb_config::using_rocksdb_in_tests (), defaults.node.rocksdb_config.enable);
	ASSERT_NE (conf.node.rocksdb_config.io_threads, defaults.node.rocksdb_config.io_threads);
	ASSERT_NE (conf.node.rocksdb_config.read_cache, defaults.node.rocksdb_config.read_cache);
	ASSERT_NE (conf.node.rocksdb_config.write_cache, defaults.node.rocksdb_config.write_cache);

	ASSERT_NE (conf.node.optimistic_scheduler.enable, defaults.node.optimistic_scheduler.enable);
	ASSERT_NE (conf.node.optimistic_scheduler.gap_threshold, defaults.node.optimistic_scheduler.gap_threshold);
	ASSERT_NE (conf.node.optimistic_scheduler.max_size, defaults.node.optimistic_scheduler.max_size);

	ASSERT_NE (conf.node.hinted_scheduler.enable, defaults.node.hinted_scheduler.enable);
	ASSERT_NE (conf.node.hinted_scheduler.hinting_threshold_percent, defaults.node.hinted_scheduler.hinting_threshold_percent);
	ASSERT_NE (conf.node.hinted_scheduler.check_interval.count (), defaults.node.hinted_scheduler.check_interval.count ());
	ASSERT_NE (conf.node.hinted_scheduler.block_cooldown.count (), defaults.node.hinted_scheduler.block_cooldown.count ());
	ASSERT_NE (conf.node.hinted_scheduler.vacancy_threshold_percent, defaults.node.hinted_scheduler.vacancy_threshold_percent);

	ASSERT_NE (conf.node.vote_cache.max_size, defaults.node.vote_cache.max_size);
	ASSERT_NE (conf.node.vote_cache.max_voters, defaults.node.vote_cache.max_voters);

	ASSERT_NE (conf.node.block_processor.max_peer_queue, defaults.node.block_processor.max_peer_queue);
	ASSERT_NE (conf.node.block_processor.max_system_queue, defaults.node.block_processor.max_system_queue);
	ASSERT_NE (conf.node.block_processor.priority_live, defaults.node.block_processor.priority_live);
	ASSERT_NE (conf.node.block_processor.priority_bootstrap, defaults.node.block_processor.priority_bootstrap);
	ASSERT_NE (conf.node.block_processor.priority_local, defaults.node.block_processor.priority_local);

	ASSERT_NE (conf.node.vote_processor.max_pr_queue, defaults.node.vote_processor.max_pr_queue);
	ASSERT_NE (conf.node.vote_processor.max_non_pr_queue, defaults.node.vote_processor.max_non_pr_queue);
	ASSERT_NE (conf.node.vote_processor.pr_priority, defaults.node.vote_processor.pr_priority);
	ASSERT_NE (conf.node.vote_processor.threads, defaults.node.vote_processor.threads);
	ASSERT_NE (conf.node.vote_processor.batch_size, defaults.node.vote_processor.batch_size);

	ASSERT_NE (conf.node.bootstrap_ascending.enable, defaults.node.bootstrap_ascending.enable);
	ASSERT_NE (conf.node.bootstrap_ascending.enable_frontier_scan, defaults.node.bootstrap_ascending.enable_frontier_scan);
	ASSERT_NE (conf.node.bootstrap_ascending.enable_database_scan, defaults.node.bootstrap_ascending.enable_database_scan);
	ASSERT_NE (conf.node.bootstrap_ascending.enable_dependency_walker, defaults.node.bootstrap_ascending.enable_dependency_walker);
	ASSERT_NE (conf.node.bootstrap_ascending.channel_limit, defaults.node.bootstrap_ascending.channel_limit);
	ASSERT_NE (conf.node.bootstrap_ascending.database_rate_limit, defaults.node.bootstrap_ascending.database_rate_limit);
	ASSERT_NE (conf.node.bootstrap_ascending.database_warmup_ratio, defaults.node.bootstrap_ascending.database_warmup_ratio);
	ASSERT_NE (conf.node.bootstrap_ascending.max_pull_count, defaults.node.bootstrap_ascending.max_pull_count);
	ASSERT_NE (conf.node.bootstrap_ascending.request_timeout, defaults.node.bootstrap_ascending.request_timeout);
	ASSERT_NE (conf.node.bootstrap_ascending.throttle_coefficient, defaults.node.bootstrap_ascending.throttle_coefficient);
	ASSERT_NE (conf.node.bootstrap_ascending.throttle_wait, defaults.node.bootstrap_ascending.throttle_wait);
	ASSERT_NE (conf.node.bootstrap_ascending.block_processor_threshold, defaults.node.bootstrap_ascending.block_processor_threshold);
	ASSERT_NE (conf.node.bootstrap_ascending.max_requests, defaults.node.bootstrap_ascending.max_requests);

	ASSERT_NE (conf.node.bootstrap_server.max_queue, defaults.node.bootstrap_server.max_queue);
	ASSERT_NE (conf.node.bootstrap_server.threads, defaults.node.bootstrap_server.threads);
	ASSERT_NE (conf.node.bootstrap_server.batch_size, defaults.node.bootstrap_server.batch_size);

	ASSERT_NE (conf.node.request_aggregator.max_queue, defaults.node.request_aggregator.max_queue);
	ASSERT_NE (conf.node.request_aggregator.threads, defaults.node.request_aggregator.threads);
	ASSERT_NE (conf.node.request_aggregator.batch_size, defaults.node.request_aggregator.batch_size);

	ASSERT_NE (conf.node.message_processor.threads, defaults.node.message_processor.threads);
	ASSERT_NE (conf.node.message_processor.max_queue, defaults.node.message_processor.max_queue);
}

/** There should be no required values **/
TEST (toml, daemon_config_no_required)
{
	std::stringstream ss;

	// A config with no values, only categories
	ss << R"toml(
	[node]
	[node.diagnostics.txn_tracking]
	[node.httpcallback]
	[node.ipc.local]
	[node.ipc.tcp]
	[node.logging]
	[node.statistics.log]
	[node.statistics.sampling]
	[node.websocket]
	[node.rocksdb]
	[opencl]
	[rpc]
	[rpc.child_process]
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);
	nano::daemon_config conf;
	nano::daemon_config defaults;
	conf.deserialize_toml (toml);

	ASSERT_FALSE (toml.get_error ()) << toml.get_error ().get_message ();
}

/** Deserialize an rpc config with non-default values */
TEST (toml, rpc_config_deserialize_no_defaults)
{
	std::stringstream ss;

	// A config file with values that differs from devnet defaults
	ss << R"toml(
	address = "0:0:0:0:0:ffff:7f01:101"
	enable_control = true
	max_json_depth = 9
	max_request_size = 999
	port = 999
	[process]
	io_threads = 999
	ipc_address = "0:0:0:0:0:ffff:7f01:101"
	ipc_port = 999
	num_ipc_connections = 999
	[logging]
	log_rpc = false
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);
	nano::rpc_config conf{ nano::dev::network_params.network };
	nano::rpc_config defaults{ nano::dev::network_params.network };
	conf.deserialize_toml (toml);

	ASSERT_FALSE (toml.get_error ()) << toml.get_error ().get_message ();

	ASSERT_NE (conf.address, defaults.address);
	ASSERT_NE (conf.enable_control, defaults.enable_control);
	ASSERT_NE (conf.max_json_depth, defaults.max_json_depth);
	ASSERT_NE (conf.max_request_size, defaults.max_request_size);
	ASSERT_NE (conf.port, defaults.port);

	ASSERT_NE (conf.rpc_process.io_threads, defaults.rpc_process.io_threads);
	ASSERT_NE (conf.rpc_process.ipc_address, defaults.rpc_process.ipc_address);
	ASSERT_NE (conf.rpc_process.ipc_port, defaults.rpc_process.ipc_port);
	ASSERT_NE (conf.rpc_process.num_ipc_connections, defaults.rpc_process.num_ipc_connections);

	ASSERT_NE (conf.rpc_logging.log_rpc, defaults.rpc_logging.log_rpc);
}

/** There should be no required values **/
TEST (toml, rpc_config_no_required)
{
	std::stringstream ss;

	// A config with no values, only categories
	ss << R"toml(
	[version]
	[process]
	[logging]
	[secure]
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);
	nano::rpc_config conf{ nano::dev::network_params.network };
	nano::rpc_config defaults{ nano::dev::network_params.network };
	conf.deserialize_toml (toml);

	ASSERT_FALSE (toml.get_error ()) << toml.get_error ().get_message ();
}

/** Deserialize a node config with incorrect values */
TEST (toml, daemon_config_deserialize_errors)
{
	{
		std::stringstream ss;
		ss << R"toml(
		[node]
		max_work_generate_multiplier = 0.9
		)toml";

		nano::tomlconfig toml;
		toml.read (ss);
		nano::daemon_config conf;
		conf.deserialize_toml (toml);

		ASSERT_EQ (toml.get_error ().get_message (), "max_work_generate_multiplier must be greater than or equal to 1");
	}
	{
		std::stringstream ss;
		ss << R"toml(
		[node]
		bootstrap_frontier_request_count = 1000
		)toml";

		nano::tomlconfig toml;
		toml.read (ss);
		nano::daemon_config conf;
		conf.deserialize_toml (toml);

		ASSERT_EQ (toml.get_error ().get_message (), "bootstrap_frontier_request_count must be greater than or equal to 1024");
	}
}

TEST (toml, daemon_read_config)
{
	auto path (nano::unique_path ());
	std::filesystem::create_directories (path);
	nano::daemon_config config;
	std::vector<std::string> invalid_overrides1{ "node.max_work_generate_multiplier=0" };
	std::string expected_message1{ "max_work_generate_multiplier must be greater than or equal to 1" };

	std::vector<std::string> invalid_overrides2{ "node.websocket.enable=true", "node.foo" };
	std::string expected_message2{ "Value must follow after a '=' at line 2" };

	// Reading when there is no config file
	ASSERT_FALSE (std::filesystem::exists (nano::get_node_toml_config_path (path)));
	ASSERT_FALSE (nano::read_node_config_toml (path, config));
	{
		auto error = nano::read_node_config_toml (path, config, invalid_overrides1);
		ASSERT_TRUE (error);
		ASSERT_EQ (error.get_message (), expected_message1);
	}
	{
		auto error = nano::read_node_config_toml (path, config, invalid_overrides2);
		ASSERT_TRUE (error);
		ASSERT_EQ (error.get_message (), expected_message2);
	}

	// Create an empty config
	nano::tomlconfig toml;
	toml.write (nano::get_node_toml_config_path (path));

	// Reading when there is a config file
	ASSERT_TRUE (std::filesystem::exists (nano::get_node_toml_config_path (path)));
	ASSERT_FALSE (nano::read_node_config_toml (path, config));
	{
		auto error = nano::read_node_config_toml (path, config, invalid_overrides1);
		ASSERT_TRUE (error);
		ASSERT_EQ (error.get_message (), expected_message1);
	}
	{
		auto error = nano::read_node_config_toml (path, config, invalid_overrides2);
		ASSERT_TRUE (error);
		ASSERT_EQ (error.get_message (), expected_message2);
	}
}

TEST (toml, log_config_defaults)
{
	std::stringstream ss;

	// A config with no values
	ss << R"toml()toml";

	nano::tomlconfig toml;
	toml.read (ss);
	nano::log_config confg{};
	nano::log_config defaults{};
	confg.deserialize_toml (toml);

	ASSERT_FALSE (toml.get_error ()) << toml.get_error ().get_message ();

	ASSERT_EQ (confg.default_level, defaults.default_level);
	ASSERT_EQ (confg.flush_level, defaults.flush_level);
	ASSERT_EQ (confg.levels, defaults.levels);
	ASSERT_EQ (confg.console.enable, defaults.console.enable);
	ASSERT_EQ (confg.console.colors, defaults.console.colors);
	ASSERT_EQ (confg.console.to_cerr, defaults.console.to_cerr);
	ASSERT_EQ (confg.file.enable, defaults.file.enable);
	ASSERT_EQ (confg.file.max_size, defaults.file.max_size);
	ASSERT_EQ (confg.file.rotation_count, defaults.file.rotation_count);
}

TEST (toml, log_config_no_defaults)
{
	std::stringstream ss;

	// A config file with values that differs from defaults
	ss << R"toml(
	[log]
	default_level = "trace"

	[log.console]
	colors = false
	enable = false
	to_cerr = true

	[log.file]
	enable = false
	max_size = 999
	rotation_count = 999

	[log.levels]
	active_elections = "trace"
	blockprocessor = "trace"
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);
	nano::log_config confg{};
	nano::log_config defaults{};
	confg.deserialize_toml (toml);

	ASSERT_FALSE (toml.get_error ()) << toml.get_error ().get_message ();

	ASSERT_NE (confg.default_level, defaults.default_level);
	ASSERT_NE (confg.levels, defaults.levels);
	ASSERT_NE (confg.console.enable, defaults.console.enable);
	ASSERT_NE (confg.console.colors, defaults.console.colors);
	ASSERT_NE (confg.console.to_cerr, defaults.console.to_cerr);
	ASSERT_NE (confg.file.enable, defaults.file.enable);
	ASSERT_NE (confg.file.max_size, defaults.file.max_size);
	ASSERT_NE (confg.file.rotation_count, defaults.file.rotation_count);
}

TEST (toml, log_config_no_required)
{
	std::stringstream ss;

	// A config with no values, only categories
	ss << R"toml(
	[log]
	[log.console]
	[log.file]
	[log.levels]
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);
	nano::log_config confg{};
	nano::log_config defaults{};
	confg.deserialize_toml (toml);

	ASSERT_FALSE (toml.get_error ()) << toml.get_error ().get_message ();
}

TEST (toml, merge_config_files)
{
	nano::network_params network_params{ nano::network_constants::active_network };
	nano::tomlconfig default_toml;
	nano::tomlconfig current_toml;
	nano::tomlconfig merged_toml;
	nano::daemon_config default_config{ ".", network_params };
	nano::daemon_config current_config{ ".", network_params };
	nano::daemon_config merged_config{ ".", network_params };

	std::stringstream ss;

	ss << R"toml(
	[node]
	 active_elections.size = 999
	 # background_threads = 7777
	[node.bootstrap_ascending]
	 block_processor_threshold = 33333
	 old_entry = 34
	)toml";

	current_toml.read (ss);
	current_config.deserialize_toml (current_toml);

	current_config.serialize_toml (current_toml);
	default_config.serialize_toml (default_toml);

	auto merged_config_string = current_toml.merge_defaults (current_toml, default_toml);

	// Configs have been merged. Let's read and parse the new config file and verify the values

	std::stringstream ss2;
	ss2 << merged_config_string;

	merged_toml.read (ss2);
	merged_config.deserialize_toml (merged_toml);

	ASSERT_NE (merged_config.node.active_elections.size, default_config.node.active_elections.size);
	ASSERT_EQ (merged_config.node.active_elections.size, 999);
	ASSERT_NE (merged_config.node.background_threads, 7777);
	ASSERT_EQ (merged_config.node.bootstrap_ascending.block_processor_threshold, 33333);
	ASSERT_TRUE (merged_config_string.find ("old_entry") == std::string::npos);
}