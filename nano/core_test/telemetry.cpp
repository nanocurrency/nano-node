#include <nano/node/telemetry.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/telemetry.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

TEST (telemetry, consolidate_data)
{
	auto time = 1582117035109;

	// Pick specific values so that we can check both mode and average are working correctly
	nano::telemetry_data data;
	data.account_count = 2;
	data.block_count = 1;
	data.cemented_count = 1;
	data.protocol_version = 12;
	data.peer_count = 2;
	data.bandwidth_cap = 100;
	data.unchecked_count = 3;
	data.uptime = 6;
	data.genesis_block = nano::block_hash (3);
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 4;
	data.pre_release_version = 6;
	data.maker = 2;
	data.timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (time));
	data.active_difficulty = 2;

	nano::telemetry_data data1;
	data1.account_count = 5;
	data1.block_count = 7;
	data1.cemented_count = 4;
	data1.protocol_version = 11;
	data1.peer_count = 5;
	data1.bandwidth_cap = 0;
	data1.unchecked_count = 1;
	data1.uptime = 10;
	data1.genesis_block = nano::block_hash (4);
	data1.major_version = 10;
	data1.minor_version = 2;
	data1.patch_version = 3;
	data1.pre_release_version = 6;
	data1.maker = 2;
	data1.timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (time + 1));
	data1.active_difficulty = 3;

	nano::telemetry_data data2;
	data2.account_count = 3;
	data2.block_count = 3;
	data2.cemented_count = 2;
	data2.protocol_version = 11;
	data2.peer_count = 4;
	data2.bandwidth_cap = 0;
	data2.unchecked_count = 2;
	data2.uptime = 3;
	data2.genesis_block = nano::block_hash (4);
	data2.major_version = 20;
	data2.minor_version = 1;
	data2.patch_version = 4;
	data2.pre_release_version = 6;
	data2.maker = 2;
	data2.timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (time));
	data2.active_difficulty = 2;

	std::vector<nano::telemetry_data> all_data{ data, data1, data2 };

	auto consolidated_telemetry_data = nano::consolidate_telemetry_data (all_data);
	ASSERT_EQ (consolidated_telemetry_data.account_count, 3);
	ASSERT_EQ (consolidated_telemetry_data.block_count, 3);
	ASSERT_EQ (consolidated_telemetry_data.cemented_count, 2);
	ASSERT_EQ (consolidated_telemetry_data.protocol_version, 11);
	ASSERT_EQ (consolidated_telemetry_data.peer_count, 3);
	ASSERT_EQ (consolidated_telemetry_data.bandwidth_cap, 0);
	ASSERT_EQ (consolidated_telemetry_data.unchecked_count, 2);
	ASSERT_EQ (consolidated_telemetry_data.uptime, 6);
	ASSERT_EQ (consolidated_telemetry_data.genesis_block, nano::block_hash (4));
	ASSERT_EQ (consolidated_telemetry_data.major_version, 20);
	ASSERT_EQ (consolidated_telemetry_data.minor_version, 1);
	ASSERT_EQ (consolidated_telemetry_data.patch_version, 4);
	ASSERT_EQ (consolidated_telemetry_data.pre_release_version, 6);
	ASSERT_EQ (consolidated_telemetry_data.maker, 2);
	ASSERT_EQ (consolidated_telemetry_data.timestamp, std::chrono::system_clock::time_point (std::chrono::milliseconds (time)));
	ASSERT_EQ (consolidated_telemetry_data.active_difficulty, 2);

	// Modify the metrics which may be either the mode or averages to ensure all are tested.
	all_data[2].bandwidth_cap = 53;
	all_data[2].protocol_version = 13;
	all_data[2].genesis_block = nano::block_hash (3);
	all_data[2].major_version = 10;
	all_data[2].minor_version = 2;
	all_data[2].patch_version = 3;
	all_data[2].pre_release_version = 6;
	all_data[2].maker = 2;

	auto consolidated_telemetry_data1 = nano::consolidate_telemetry_data (all_data);
	ASSERT_EQ (consolidated_telemetry_data1.major_version, 10);
	ASSERT_EQ (consolidated_telemetry_data1.minor_version, 2);
	ASSERT_EQ (consolidated_telemetry_data1.patch_version, 3);
	ASSERT_EQ (consolidated_telemetry_data1.pre_release_version, 6);
	ASSERT_EQ (consolidated_telemetry_data1.maker, 2);
	ASSERT_TRUE (consolidated_telemetry_data1.protocol_version == 11 || consolidated_telemetry_data1.protocol_version == 12 || consolidated_telemetry_data1.protocol_version == 13);
	ASSERT_EQ (consolidated_telemetry_data1.bandwidth_cap, 51);
	ASSERT_EQ (consolidated_telemetry_data1.genesis_block, nano::block_hash (3));

	// Test equality operator
	ASSERT_FALSE (consolidated_telemetry_data == consolidated_telemetry_data1);
	ASSERT_EQ (consolidated_telemetry_data, consolidated_telemetry_data);
}

TEST (telemetry, consolidate_data_remove_outliers)
{
	nano::telemetry_data data;
	data.account_count = 2;
	data.block_count = 1;
	data.cemented_count = 1;
	data.protocol_version = 12;
	data.peer_count = 2;
	data.bandwidth_cap = 100;
	data.unchecked_count = 3;
	data.uptime = 6;
	data.genesis_block = nano::block_hash (3);
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = 1;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.active_difficulty = 10;

	// Insert 20 of these, and 2 outliers at the lower and upper bounds which should get removed
	std::vector<nano::telemetry_data> all_data (20, data);

	// Insert some outliers
	nano::telemetry_data lower_bound_outlier_data;
	lower_bound_outlier_data.account_count = 1;
	lower_bound_outlier_data.block_count = 0;
	lower_bound_outlier_data.cemented_count = 0;
	lower_bound_outlier_data.protocol_version = 11;
	lower_bound_outlier_data.peer_count = 0;
	lower_bound_outlier_data.bandwidth_cap = 8;
	lower_bound_outlier_data.unchecked_count = 1;
	lower_bound_outlier_data.uptime = 2;
	lower_bound_outlier_data.genesis_block = nano::block_hash (2);
	lower_bound_outlier_data.major_version = 11;
	lower_bound_outlier_data.minor_version = 1;
	lower_bound_outlier_data.patch_version = 1;
	lower_bound_outlier_data.pre_release_version = 1;
	lower_bound_outlier_data.maker = 1;
	lower_bound_outlier_data.timestamp = std::chrono::system_clock::time_point (1ms);
	lower_bound_outlier_data.active_difficulty = 1;
	all_data.push_back (lower_bound_outlier_data);
	all_data.push_back (lower_bound_outlier_data);

	nano::telemetry_data upper_bound_outlier_data;
	upper_bound_outlier_data.account_count = 99;
	upper_bound_outlier_data.block_count = 99;
	upper_bound_outlier_data.cemented_count = 99;
	upper_bound_outlier_data.protocol_version = 99;
	upper_bound_outlier_data.peer_count = 99;
	upper_bound_outlier_data.bandwidth_cap = 999;
	upper_bound_outlier_data.unchecked_count = 99;
	upper_bound_outlier_data.uptime = 999;
	upper_bound_outlier_data.genesis_block = nano::block_hash (99);
	upper_bound_outlier_data.major_version = 99;
	upper_bound_outlier_data.minor_version = 9;
	upper_bound_outlier_data.patch_version = 9;
	upper_bound_outlier_data.pre_release_version = 9;
	upper_bound_outlier_data.maker = 9;
	upper_bound_outlier_data.timestamp = std::chrono::system_clock::time_point (999ms);
	upper_bound_outlier_data.active_difficulty = 99;
	all_data.push_back (upper_bound_outlier_data);
	all_data.push_back (upper_bound_outlier_data);

	auto consolidated_telemetry_data = nano::consolidate_telemetry_data (all_data);
	ASSERT_EQ (data, consolidated_telemetry_data);
}

TEST (telemetry, consolidate_data_remove_outliers_with_zero_bandwidth)
{
	nano::telemetry_data data1;
	data1.account_count = 2;
	data1.block_count = 1;
	data1.cemented_count = 1;
	data1.protocol_version = 12;
	data1.peer_count = 2;
	data1.bandwidth_cap = 0;
	data1.unchecked_count = 3;
	data1.uptime = 6;
	data1.genesis_block = nano::block_hash (3);
	data1.major_version = 20;
	data1.minor_version = 1;
	data1.patch_version = 5;
	data1.pre_release_version = 2;
	data1.maker = 1;
	data1.timestamp = std::chrono::system_clock::time_point (100ms);
	data1.active_difficulty = 10;

	// Add a majority of nodes with bandwidth set to 0
	std::vector<nano::telemetry_data> all_data (100, data1);

	nano::telemetry_data data2;
	data2.account_count = 2;
	data2.block_count = 1;
	data2.cemented_count = 1;
	data2.protocol_version = 12;
	data2.peer_count = 2;
	data2.bandwidth_cap = 100;
	data2.unchecked_count = 3;
	data2.uptime = 6;
	data2.genesis_block = nano::block_hash (3);
	data2.major_version = 20;
	data2.minor_version = 1;
	data2.patch_version = 5;
	data2.pre_release_version = 2;
	data2.maker = 1;
	data2.timestamp = std::chrono::system_clock::time_point (100ms);
	data2.active_difficulty = 10;

	auto consolidated_telemetry_data1 = nano::consolidate_telemetry_data (all_data);
	ASSERT_EQ (consolidated_telemetry_data1.bandwidth_cap, 0);

	// And a few nodes with non-zero bandwidth
	all_data.push_back (data2);
	all_data.push_back (data2);

	auto consolidated_telemetry_data2 = nano::consolidate_telemetry_data (all_data);
	ASSERT_EQ (consolidated_telemetry_data2.bandwidth_cap, 0);
}

TEST (telemetry, signatures)
{
	nano::keypair node_id;
	nano::telemetry_data data;
	data.node_id = node_id.pub;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = 1;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.sign (node_id);
	ASSERT_FALSE (data.validate_signature ());
	auto signature = data.signature;
	// Check that the signature is different if changing a piece of data
	data.maker = 2;
	data.sign (node_id);
	ASSERT_NE (data.signature, signature);
}

TEST (telemetry, unknown_data)
{
	nano::keypair node_id;
	nano::telemetry_data data;
	data.node_id = node_id.pub;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = 1;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.unknown_data.push_back (1);
	data.sign (node_id);
	ASSERT_FALSE (data.validate_signature ());
}

TEST (telemetry, no_peers)
{
	nano::test::system system (1);

	auto responses = system.nodes[0]->telemetry.get_all_telemetries ();
	ASSERT_TRUE (responses.empty ());
}

TEST (telemetry, basic)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	node_flags.disable_ongoing_telemetry_requests = true;
	auto node_server = system.add_node (node_flags);

	nano::test::wait_peer_connections (system);

	// Request telemetry metrics
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	std::optional<nano::telemetry_data> telemetry_data;
	ASSERT_TIMELY (5s, telemetry_data = node_client->telemetry.get_telemetry (channel->get_endpoint ()));
	ASSERT_EQ (node_server->get_node_id (), telemetry_data->node_id);

	// Check the metrics are correct
	ASSERT_TRUE (nano::test::compare_telemetry (*telemetry_data, *node_server));

	// Call again straight away
	auto telemetry_data_2 = node_client->telemetry.get_telemetry (channel->get_endpoint ());
	ASSERT_TRUE (telemetry_data_2);

	// Call again straight away
	auto telemetry_data_3 = node_client->telemetry.get_telemetry (channel->get_endpoint ());
	ASSERT_TRUE (telemetry_data_3);

	// we expect at least one consecutive repeat of telemetry
	ASSERT_TRUE (*telemetry_data == telemetry_data_2 || telemetry_data_2 == telemetry_data_3);

	// Wait the cache period and check cache is not used
	WAIT (3s);

	std::optional<nano::telemetry_data> telemetry_data_4;
	ASSERT_TIMELY (5s, telemetry_data_4 = node_client->telemetry.get_telemetry (channel->get_endpoint ()));
	ASSERT_NE (*telemetry_data, *telemetry_data_4);
}

TEST (telemetry, invalid_endpoint)
{
	nano::test::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	node_client->telemetry.trigger ();

	// Give some time for nodes to exchange telemetry
	WAIT (1s);

	nano::endpoint endpoint = *nano::parse_endpoint ("::ffff:240.0.0.0:12345");
	ASSERT_FALSE (node_client->telemetry.get_telemetry (endpoint));
}

TEST (telemetry, disconnected)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);
	nano::test::wait_peer_connections (system);
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	// Ensure telemetry is available before disconnecting
	ASSERT_TIMELY (5s, node_client->telemetry.get_telemetry (channel->get_endpoint ()));

	system.stop_node (*node_server);
	ASSERT_TRUE (channel);

	// Ensure telemetry from disconnected peer is removed
	ASSERT_TIMELY (5s, !node_client->telemetry.get_telemetry (channel->get_endpoint ()));
}

TEST (telemetry, dos_tcp)
{
	// Confirm that telemetry_reqs are not processed
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_ongoing_telemetry_requests = true;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	nano::test::wait_peer_connections (system);

	nano::telemetry_req message{ nano::dev::network_params.network };
	auto channel = node_client->network.tcp_channels.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);
	channel->send (message, [] (boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
	});

	ASSERT_TIMELY_EQ (5s, 1, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));

	auto orig = std::chrono::steady_clock::now ();
	for (int i = 0; i < 10; ++i)
	{
		channel->send (message, [] (boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
		});
	}

	ASSERT_TIMELY (5s, (nano::dev::network_params.network.telemetry_request_cooldown + orig) <= std::chrono::steady_clock::now ());

	// Should process no more telemetry_req messages
	ASSERT_EQ (1, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));

	// Now spam messages waiting for it to be processed
	while (node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in) == 1)
	{
		channel->send (message);
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (telemetry, disable_metrics)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	node_flags.disable_providing_telemetry_metrics = true;
	auto node_server = system.add_node (node_flags);

	nano::test::wait_peer_connections (system);

	// Try and request metrics from a node which is turned off but a channel is not closed yet
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	node_client->telemetry.trigger ();

	ASSERT_NEVER (1s, node_client->telemetry.get_telemetry (channel->get_endpoint ()));

	// It should still be able to receive metrics though
	auto channel1 = node_server->network.find_node_id (node_client->get_node_id ());
	ASSERT_NE (nullptr, channel1);

	std::optional<nano::telemetry_data> telemetry_data;
	ASSERT_TIMELY (5s, telemetry_data = node_server->telemetry.get_telemetry (channel1->get_endpoint ()));

	ASSERT_TRUE (nano::test::compare_telemetry (*telemetry_data, *node_client));
}

TEST (telemetry, max_possible_size)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_ongoing_telemetry_requests = true;
	node_flags.disable_providing_telemetry_metrics = true;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	nano::telemetry_data data;
	data.unknown_data.resize (nano::message_header::telemetry_size_mask.to_ulong () - nano::telemetry_data::latest_size);

	nano::telemetry_ack message{ nano::dev::network_params.network, data };
	nano::test::wait_peer_connections (system);

	auto channel = node_client->network.tcp_channels.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);
	channel->send (message, [] (boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
	});

	ASSERT_TIMELY_EQ (5s, 1, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_ack, nano::stat::dir::in));
}

TEST (telemetry, maker_pruning)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	node_flags.enable_pruning = true;
	nano::node_config config;
	config.enable_voting = false;
	node_flags.disable_ongoing_telemetry_requests = true;
	auto node_server = system.add_node (config, node_flags);

	nano::test::wait_peer_connections (system);

	// Request telemetry metrics
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	std::optional<nano::telemetry_data> telemetry_data;
	ASSERT_TIMELY (5s, telemetry_data = node_client->telemetry.get_telemetry (channel->get_endpoint ()));
	ASSERT_EQ (node_server->get_node_id (), telemetry_data->node_id);

	// Ensure telemetry response indicates pruned node
	ASSERT_EQ (nano::telemetry_maker::nf_pruned_node, static_cast<nano::telemetry_maker> (telemetry_data->maker));
}

TEST (telemetry, invalid_signature)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto telemetry = node.local_telemetry ();
	telemetry.block_count = 9999; // Change data so signature is no longer valid

	auto message = nano::telemetry_ack{ nano::dev::network_params.network, telemetry };
	node.network.inbound (message, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::invalid_signature) > 0);
	ASSERT_ALWAYS (1s, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) == 0)
}

TEST (telemetry, mismatched_node_id)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto telemetry = node.local_telemetry ();

	auto message = nano::telemetry_ack{ nano::dev::network_params.network, telemetry };
	node.network.inbound (message, nano::test::fake_channel (node, /* node id */ { 123 }));

	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::node_id_mismatch) > 0);
	ASSERT_ALWAYS (1s, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) == 0)
}

TEST (telemetry, ongoing_broadcasts)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_ongoing_telemetry_requests = true;
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);

	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) >= 3);
	ASSERT_TIMELY (5s, node2.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) >= 3)
}

// TODO: With handshake V2, nodes with mismatched genesis will refuse to connect while setting up the system
TEST (telemetry, DISABLED_mismatched_genesis)
{
	// Only second node will broadcast telemetry
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_ongoing_telemetry_requests = true;
	node_flags.disable_providing_telemetry_metrics = true;
	auto & node1 = *system.add_node (node_flags);

	// Set up a node with different genesis
	nano::network_params network_params{ nano::networks::nano_dev_network };
	network_params.ledger.genesis = network_params.ledger.nano_live_genesis;
	nano::node_config node_config{ network_params };
	node_flags.disable_providing_telemetry_metrics = false;
	auto & node2 = *system.add_node (node_config, node_flags);

	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::telemetry, nano::stat::detail::genesis_mismatch) > 0);
	ASSERT_ALWAYS (1s, node1.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) == 0)

	// Ensure node with different genesis gets disconnected
	ASSERT_TIMELY (5s, !node1.network.find_node_id (node2.get_node_id ()));
}

TEST (telemetry, majority_database_backend_information_missing)
{
	// Majority of nodes reporting no database info (Version 26.1 and earlier). One node reporting RocksDb backend
	nano::telemetry_data data1;
	data1.account_count = 2;
	data1.block_count = 1;
	data1.cemented_count = 1;
	data1.protocol_version = 12;
	data1.peer_count = 2;
	data1.bandwidth_cap = 0;
	data1.unchecked_count = 3;
	data1.uptime = 6;
	data1.genesis_block = nano::block_hash (3);
	data1.major_version = 27;
	data1.minor_version = 0;
	data1.patch_version = 0;
	data1.pre_release_version = 1;
	data1.maker = 1;
	data1.timestamp = std::chrono::system_clock::time_point (100ms);
	data1.active_difficulty = 10;
	std::vector<nano::telemetry_data> all_data (100, data1);

	nano::telemetry_data data2;
	data2.account_count = 2;
	data2.block_count = 1;
	data2.cemented_count = 1;
	data2.protocol_version = 12;
	data2.peer_count = 2;
	data2.bandwidth_cap = 100;
	data2.unchecked_count = 3;
	data2.uptime = 6;
	data2.genesis_block = nano::block_hash (3);
	data2.major_version = 27;
	data2.minor_version = 0;
	data2.patch_version = 0;
	data2.pre_release_version = 2;
	data2.maker = 1;
	data2.timestamp = std::chrono::system_clock::time_point (100ms);
	data2.active_difficulty = 10;
	data1.database_backend = "RocksDb";

	all_data.push_back (data2);

	auto consolidated_telemetry_data2 = nano::consolidate_telemetry_data (all_data);
	ASSERT_EQ (consolidated_telemetry_data2.database_backend, "Unknown");
}

TEST (telemetry, majority_database_backend_information_included)
{
	// Majority of nodes with LMDB database. One node with no information
	nano::telemetry_data data1;
	data1.account_count = 2;
	data1.block_count = 1;
	data1.cemented_count = 1;
	data1.protocol_version = 12;
	data1.peer_count = 2;
	data1.bandwidth_cap = 0;
	data1.unchecked_count = 3;
	data1.uptime = 6;
	data1.genesis_block = nano::block_hash (3);
	data1.major_version = 27;
	data1.minor_version = 0;
	data1.patch_version = 0;
	data1.pre_release_version = 1;
	data1.maker = 1;
	data1.timestamp = std::chrono::system_clock::time_point (100ms);
	data1.active_difficulty = 10;
	data1.database_backend = "LMDB";
	std::vector<nano::telemetry_data> all_data (100, data1);

	nano::telemetry_data data2;
	data2.account_count = 2;
	data2.block_count = 1;
	data2.cemented_count = 1;
	data2.protocol_version = 12;
	data2.peer_count = 2;
	data2.bandwidth_cap = 100;
	data2.unchecked_count = 3;
	data2.uptime = 6;
	data2.genesis_block = nano::block_hash (3);
	data2.major_version = 27;
	data2.minor_version = 0;
	data2.patch_version = 0;
	data2.pre_release_version = 2;
	data2.maker = 1;
	data2.timestamp = std::chrono::system_clock::time_point (100ms);
	data2.active_difficulty = 10;

	all_data.push_back (data2);

	auto consolidated_telemetry_data2 = nano::consolidate_telemetry_data (all_data);
	ASSERT_EQ (consolidated_telemetry_data2.database_backend, "LMDB");
}