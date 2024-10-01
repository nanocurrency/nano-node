#include <nano/node/telemetry.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/telemetry.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

TEST (telemetry, signatures)
{
	nano::keypair node_id;
	nano::telemetry_data data;
	data.node_id = node_id.pub;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = nano::telemetry_maker::nf_pruned_node;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.sign (node_id);
	ASSERT_FALSE (data.validate_signature ());
	auto signature = data.signature;
	// Check that the signature is different if changing a piece of data
	data.maker = nano::telemetry_maker::nf_node;
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
	node_flags.disable_providing_telemetry_metrics = true;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	nano::telemetry_data data;
	data.unknown_data.resize (nano::message_header::telemetry_size_mask.to_ulong () - nano::telemetry_data::size);

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
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);

	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) >= 3);
	ASSERT_TIMELY (5s, node2.stats.count (nano::stat::type::telemetry, nano::stat::detail::process) >= 3)
}
