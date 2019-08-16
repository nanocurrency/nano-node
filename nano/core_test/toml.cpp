#include <nano/core_test/testutil.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

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
	[node]
	allow_local_peers = false
	block_processor_batch_max_time = 5000
	preconfigured_peers = ["peer1.org", "peer2.org"]
	same_array = ["1", "2"]

	[node.diagnostics.txn_tracking]
	enable = true

	[rpc]
	enable = false
	enable_sign_hash = true
	max_work_generate_difficulty = "ffffffffc0001234"
	)toml";

	defaults.read (ss);

	// User file. The rpc section is the same and doesn't need to be emitted
	std::stringstream ss_override;
	ss_override << R"toml(
	[node]
	allow_local_peers = true
	block_processor_batch_max_time = 5000
	preconfigured_peers = ["peer1.org", "peer2.org", "peer3.org"]
	same_array = ["1", "2"]

	[node.diagnostics.txn_tracking]
	enable = false

	[rpc]
	enable = false
	enable_sign_hash = true
	max_work_generate_difficulty = "ffffffffc0001234"
	)toml";

	other.read (ss_override);

	other.erase_default_values (defaults);
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
	boost::filesystem::path data_path (".");
	nano::daemon_config c (data_path);
	c.node.preconfigured_peers.push_back ("test-peer.org");
	c.serialize_toml (t);
	c.deserialize_toml (t);
	ASSERT_EQ (c.node.preconfigured_peers[0], "test-peer.org");
}

/** Deserialize a toml file with non-default values */
TEST (toml, daemon_config_deserialize)
{
	std::stringstream ss;
	ss << R"toml(
		[node]
		active_elections_size = 50000
		allow_local_peers = true
		bandwidth_limit = 5242880
		block_processor_batch_max_time = 5000
		bootstrap_connections = 4
		bootstrap_connections_max = 64
		bootstrap_fraction_numerator = 1
		callback_address = ""
		callback_port = 0
		callback_target = ""
		confirmation_history_size = 2048
		enable_voting = true
		external_address = "::"
		external_port = 0
		io_threads = 4
		lmdb_max_dbs = 128
		network_threads = 4
		online_weight_minimum = "60000000000000000000000000000000000000"
		online_weight_quorum = 50
		password_fanout = 1024
		peering_port = 44000
		pow_sleep_interval = 0
		preconfigured_peers = ["test-peer.org"]
		preconfigured_representatives = ["nano_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo"]
		receive_minimum = "1000000000000000000000000"
		signature_checker_threads = 3
		tcp_incoming_connections_max = 1024
		tcp_io_timeout = 5
		unchecked_cutoff_time = 14400
		use_memory_pools = true
		vote_generator_delay = 100
		vote_generator_threshold = 3
		vote_minimum = "1000000000000000000000000000000000"
		work_peers = []
		work_threads = 4

		[node.diagnostics.txn_tracking]
		enable = true
		ignore_writes_below_block_processor_max_time = false
		min_read_txn_time = 1
		min_write_txn_time = 2

		[node.logging]
		bulk_pull = true
		flush = true
		insufficient_work = true
		ledger = true
		ledger_duplicate = true
		log_ipc = true
		log_to_cerr = true
		max_size = 1
		min_time_between_output = 5
		network = true
		network_keepalive = true
		network_message = true
		network_node_id_handshake = true
		network_packet = true
		network_publish = true
		network_timeout = true
		node_lifetime_tracing = true
		rotation_size = 2
		timing = true
		upnp_details = true
		vote = true
		work_generation_time = true

		[node.websocket]
		enable = true
		address = "0:0:0:0:0:ffff:7f01:101"
		port = 1234

		[node.ipc.local]
		allow_unsafe = true
		enable = true
		io_timeout = 20
		path = "/tmp/test"

		[node.ipc.tcp]
		enable = true
		io_timeout = 20
		port = 10000

		[node.statistics.log]
		headers = false
		filename_counters = "test1.stat"
		filename_samples = "test2.stat"
		interval_counters = 1
		interval_samples = 2
		rotation_count = 3

		[node.statistics.sampling]
		enable = true
		interval = 1
		capacity = 2

		[opencl]
		enable = true
		device = 1
		platform = 2
		threads = 3

		[rpc]
		enable = true
		enable_sign_hash = true
		max_work_generate_difficulty = "ffffffffc0001234"

		[rpc.child_process]
		enable = true
		rpc_path = "/my/path"
	)toml";

	nano::tomlconfig toml;
	toml.read (ss);
	nano::daemon_config conf;
	conf.deserialize_toml (toml);
	// Verify that items of various types parse correctly
	ASSERT_FALSE (toml.get_error ()) << toml.get_error ().get_message ();
	ASSERT_TRUE (conf.opencl_enable);
	ASSERT_EQ (conf.opencl.device, 1);
	ASSERT_EQ (conf.opencl.platform, 2);
	ASSERT_EQ (conf.opencl.threads, 3);
	ASSERT_EQ (conf.rpc_enable, true);
	ASSERT_EQ (conf.rpc.enable_sign_hash, true);
	ASSERT_EQ (conf.rpc.max_work_generate_difficulty, 0xffffffffc0001234);
	ASSERT_EQ (conf.rpc.child_process.enable, true);
	ASSERT_EQ (conf.rpc.child_process.rpc_path, "/my/path");
	ASSERT_EQ (conf.node.preconfigured_peers[0], "test-peer.org");
	ASSERT_EQ (conf.node.receive_minimum.to_string_dec (), "1000000000000000000000000");
	ASSERT_EQ (conf.node.peering_port, 44000);
	ASSERT_EQ (conf.node.logging.bulk_pull_logging_value, true);
	ASSERT_EQ (conf.node.logging.max_size, 1);
	ASSERT_EQ (conf.node.websocket_config.enabled, true);
	boost::system::error_code bec;
	auto address_l (boost::asio::ip::address_v6::from_string ("0:0:0:0:0:ffff:7f01:101", bec));
	ASSERT_EQ (conf.node.websocket_config.address, address_l);
	ASSERT_EQ (conf.node.websocket_config.port, 1234);
	ASSERT_EQ (conf.node.ipc_config.transport_domain.allow_unsafe, true);
	ASSERT_EQ (conf.node.ipc_config.transport_domain.enabled, true);
	ASSERT_EQ (conf.node.ipc_config.transport_domain.io_timeout, 20);
	ASSERT_EQ (conf.node.ipc_config.transport_domain.path, "/tmp/test");
	ASSERT_EQ (conf.node.ipc_config.transport_tcp.enabled, true);
	ASSERT_EQ (conf.node.ipc_config.transport_tcp.io_timeout, 20);
	ASSERT_EQ (conf.node.ipc_config.transport_tcp.port, 10000);
	ASSERT_EQ (conf.node.diagnostics_config.txn_tracking.enable, true);
	ASSERT_EQ (conf.node.stat_config.sampling_enabled, true);
	ASSERT_EQ (conf.node.stat_config.interval, 1);
	ASSERT_EQ (conf.node.stat_config.capacity, 2);
	ASSERT_EQ (conf.node.stat_config.log_headers, false);
	ASSERT_EQ (conf.node.stat_config.log_counters_filename, "test1.stat");
	ASSERT_EQ (conf.node.stat_config.log_samples_filename, "test2.stat");
}

/** Empty config file should match a default config object */
TEST (toml, daemon_config_deserialize_defaults)
{
	std::stringstream ss;
	ss << R"toml(
	)toml";

	nano::tomlconfig t;
	t.read (ss);
	nano::daemon_config c;
	nano::daemon_config defaults;
	c.deserialize_toml (t);
	ASSERT_EQ (c.opencl_enable, defaults.opencl_enable);
	ASSERT_EQ (c.opencl.device, defaults.opencl.device);
	ASSERT_EQ (c.opencl.platform, defaults.opencl.platform);
	ASSERT_EQ (c.opencl.threads, defaults.opencl.threads);
	ASSERT_EQ (c.rpc.enable_sign_hash, false);
	ASSERT_EQ (c.rpc.max_work_generate_difficulty, 0xffffffffc0000000);
	ASSERT_EQ (c.rpc.child_process.enable, false);
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
	ASSERT_TRUE (t.get_error () == nano::error_config::missing_value);
	t.get_error ().clear ();

	// Query uint16 that's too big, make sure we have an error
	t.get_required<uint16_t> ("node.too_big", port);
	ASSERT_TRUE (t.get_error ());
	ASSERT_TRUE (t.get_error () == nano::error_config::invalid_value);
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
	config_node.array_entries_required<std::string> ("items", [&i](std::string item) {
		ASSERT_TRUE (item == std::string ("item ") + std::to_string (i));
		i++;
	});
}
