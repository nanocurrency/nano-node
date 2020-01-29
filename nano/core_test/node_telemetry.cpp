#include <nano/core_test/testutil.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

namespace
{
void wait_peer_connections (nano::system & system_a);
void compare_default_test_result_data (nano::telemetry_data & telemetry_data_a, nano::node const & node_server_a);
}

TEST (node_telemetry, consolidate_data)
{
	nano::telemetry_data data;
	data.account_count = 2;
	data.block_count = 1;
	data.cemented_count = 1;
	data.vendor_version = 20;
	data.protocol_version_number = 12;
	data.peer_count = 2;
	data.bandwidth_cap = 100;
	data.unchecked_count = 3;
	data.uptime = 6;
	data.genesis_block = nano::block_hash (3);

	nano::telemetry_data data1;
	data1.account_count = 5;
	data1.block_count = 7;
	data1.cemented_count = 4;
	data1.vendor_version = 10;
	data1.protocol_version_number = 11;
	data1.peer_count = 5;
	data1.bandwidth_cap = 0;
	data1.unchecked_count = 1;
	data1.uptime = 10;
	data1.genesis_block = nano::block_hash (4);

	nano::telemetry_data data2;
	data2.account_count = 3;
	data2.block_count = 3;
	data2.cemented_count = 2;
	data2.vendor_version = 20;
	data2.protocol_version_number = 11;
	data2.peer_count = 4;
	data2.bandwidth_cap = 0;
	data2.unchecked_count = 2;
	data2.uptime = 3;
	data2.genesis_block = nano::block_hash (4);

	std::vector<nano::telemetry_data> all_data{ data, data1, data2 };

	auto consolidated_telemetry_data = nano::telemetry_data::consolidate (all_data);
	ASSERT_EQ (consolidated_telemetry_data.account_count, 3);
	ASSERT_EQ (consolidated_telemetry_data.block_count, 3);
	ASSERT_EQ (consolidated_telemetry_data.cemented_count, 2);
	ASSERT_EQ (consolidated_telemetry_data.vendor_version, 20);
	ASSERT_EQ (consolidated_telemetry_data.protocol_version_number, 11);
	ASSERT_EQ (consolidated_telemetry_data.peer_count, 3);
	ASSERT_EQ (consolidated_telemetry_data.bandwidth_cap, 0);
	ASSERT_EQ (consolidated_telemetry_data.unchecked_count, 2);
	ASSERT_EQ (consolidated_telemetry_data.uptime, 6);
	ASSERT_EQ (consolidated_telemetry_data.genesis_block, nano::block_hash (4));

	// Modify the metrics which may be either the mode or averages to ensure all are tested.
	all_data[2].bandwidth_cap = 53;
	all_data[2].protocol_version_number = 13;
	all_data[2].vendor_version = 13;
	all_data[2].genesis_block = nano::block_hash (3);

	auto consolidated_telemetry_data1 = nano::telemetry_data::consolidate (all_data);
	ASSERT_TRUE (consolidated_telemetry_data1.vendor_version == 10 || consolidated_telemetry_data1.vendor_version == 13 || consolidated_telemetry_data1.vendor_version == 20);
	ASSERT_TRUE (consolidated_telemetry_data1.protocol_version_number == 11 || consolidated_telemetry_data1.protocol_version_number == 12 || consolidated_telemetry_data1.protocol_version_number == 13);
	ASSERT_EQ (consolidated_telemetry_data1.bandwidth_cap, 51);
	ASSERT_EQ (consolidated_telemetry_data1.genesis_block, nano::block_hash (3));

	// Test equality operator
	ASSERT_FALSE (consolidated_telemetry_data == consolidated_telemetry_data1);
	ASSERT_EQ (consolidated_telemetry_data, consolidated_telemetry_data);
}

TEST (node_telemetry, consolidate_data_remove_outliers)
{
	nano::telemetry_data data;
	data.account_count = 2;
	data.block_count = 1;
	data.cemented_count = 1;
	data.vendor_version = 20;
	data.protocol_version_number = 12;
	data.peer_count = 2;
	data.bandwidth_cap = 100;
	data.unchecked_count = 3;
	data.uptime = 6;
	data.genesis_block = nano::block_hash (3);

	// Insert 20 of these, and 2 outliers at the lower and upper bounds which should get removed
	std::vector<nano::telemetry_data> all_data (20, data);

	// Insert some outliers
	nano::telemetry_data outlier_data;
	outlier_data.account_count = 1;
	outlier_data.block_count = 0;
	outlier_data.cemented_count = 0;
	outlier_data.vendor_version = 11;
	outlier_data.protocol_version_number = 11;
	outlier_data.peer_count = 0;
	outlier_data.bandwidth_cap = 8;
	outlier_data.unchecked_count = 1;
	outlier_data.uptime = 2;
	outlier_data.genesis_block = nano::block_hash (2);
	all_data.push_back (outlier_data);
	all_data.push_back (outlier_data);

	nano::telemetry_data outlier_data1;
	outlier_data1.account_count = 99;
	outlier_data1.block_count = 99;
	outlier_data1.cemented_count = 99;
	outlier_data1.vendor_version = 99;
	outlier_data1.protocol_version_number = 99;
	outlier_data1.peer_count = 99;
	outlier_data1.bandwidth_cap = 999;
	outlier_data1.unchecked_count = 99;
	outlier_data1.uptime = 999;
	outlier_data1.genesis_block = nano::block_hash (99);
	all_data.push_back (outlier_data1);
	all_data.push_back (outlier_data1);

	auto consolidated_telemetry_data = nano::telemetry_data::consolidate (all_data);
	ASSERT_EQ (data, consolidated_telemetry_data);
}

TEST (node_telemetry, no_peers)
{
	nano::system system (1);

	std::atomic<bool> done{ false };
	system.nodes[0]->telemetry.get_metrics_peers_async ([&done](nano::telemetry_data_responses const & responses_a) {
		ASSERT_TRUE (responses_a.telemetry_data_time_pairs.empty ());
		ASSERT_FALSE (responses_a.all_received);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

namespace nano
{
TEST (node_telemetry, basic)
{
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	wait_peer_connections (system);

	// Request telemetry metrics
	std::unordered_map<nano::endpoint, nano::telemetry_data_time_pair> all_telemetry_data_time_pairs;
	{
		std::atomic<bool> done{ false };
		node_client->telemetry.get_metrics_peers_async ([&done, &all_telemetry_data_time_pairs](nano::telemetry_data_responses const & responses_a) {
			ASSERT_TRUE (responses_a.all_received);
			all_telemetry_data_time_pairs = responses_a.telemetry_data_time_pairs;
			done = true;
		});

		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}

	// Check the metrics are correct
	ASSERT_EQ (all_telemetry_data_time_pairs.size (), 1);
	compare_default_test_result_data (all_telemetry_data_time_pairs.begin ()->second.data, *node_server);

	// Call again straight away. It should use the cache
	{
		std::atomic<bool> done{ false };
		node_client->telemetry.get_metrics_peers_async ([&done, &all_telemetry_data_time_pairs](nano::telemetry_data_responses const & responses_a) {
			ASSERT_EQ (all_telemetry_data_time_pairs, responses_a.telemetry_data_time_pairs);
			ASSERT_TRUE (responses_a.all_received);
			done = true;
		});

		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}

	// Wait the cache period and check cache is not used
	std::this_thread::sleep_for (nano::telemetry_cache_cutoffs::test);
	// Arbitrarily change something so that we can confirm different metrics were used
	node_server->ledger.cache.block_count = 100;

	std::atomic<bool> done{ false };
	node_client->telemetry.get_metrics_peers_async ([&done, &all_telemetry_data_time_pairs](nano::telemetry_data_responses const & responses_a) {
		ASSERT_NE (all_telemetry_data_time_pairs, responses_a.telemetry_data_time_pairs);
		ASSERT_TRUE (responses_a.all_received);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}
}

TEST (node_telemetry, many_nodes)
{
	nano::system system;
	// The telemetry responses can timeout if using a large number of nodes under sanitizers, so lower the number.
	const auto num_nodes = (is_sanitizer_build || nano::running_within_valgrind ()) ? 4 : 10;
	for (auto i = 0; i < num_nodes; ++i)
	{
		nano::node_config node_config (nano::get_available_port (), system.logging);
		// Make a metric completely different for each node so we can get afterwards that there are no duplicates
		node_config.bandwidth_limit = 100000 + i;
		system.add_node (node_config);
	}

	wait_peer_connections (system);

	// Give all nodes a non-default number of blocks
	nano::keypair key;
	nano::genesis genesis;
	nano::state_block send (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::Mxrb_ratio, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (genesis.hash ()));
	for (auto node : system.nodes)
	{
		auto transaction (node->store.tx_begin_write ());
		ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, send).code);
	}

	// This is the node which will request metrics from all other nodes
	auto node_client = system.nodes.front ();

	std::atomic<bool> done{ false };
	std::unordered_map<nano::endpoint, nano::telemetry_data_time_pair> all_telemetry_data_time_pairs;
	node_client->telemetry.get_metrics_peers_async ([&done, &all_telemetry_data_time_pairs](nano::telemetry_data_responses const & responses_a) {
		ASSERT_TRUE (responses_a.all_received);
		all_telemetry_data_time_pairs = responses_a.telemetry_data_time_pairs;
		done = true;
	});

	system.deadline_set (20s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Check the metrics
	nano::network_params params;
	for (auto & telemetry_data_time_pair : all_telemetry_data_time_pairs)
	{
		auto & data = telemetry_data_time_pair.second.data;
		ASSERT_EQ (data.unchecked_count, 0);
		ASSERT_EQ (data.cemented_count, 1);
		ASSERT_LE (data.peer_count, 9);
		ASSERT_EQ (data.account_count, 1);
		ASSERT_TRUE (data.block_count == 2);
		ASSERT_EQ (data.protocol_version_number, params.protocol.telemetry_protocol_version_min);
		ASSERT_GE (data.bandwidth_cap, 100000);
		ASSERT_LT (data.bandwidth_cap, 100000 + system.nodes.size ());
		ASSERT_EQ (data.vendor_version, nano::get_major_node_version ());
		ASSERT_LT (data.uptime, 100);
		ASSERT_EQ (data.genesis_block, genesis.hash ());
	}

	// We gave some nodes different bandwidth caps, confirm they are not all the time
	auto bandwidth_cap = all_telemetry_data_time_pairs.begin ()->second.data.bandwidth_cap;
	all_telemetry_data_time_pairs.erase (all_telemetry_data_time_pairs.begin ());
	auto all_bandwidth_limits_same = std::all_of (all_telemetry_data_time_pairs.begin (), all_telemetry_data_time_pairs.end (), [bandwidth_cap](auto & telemetry_data_time_pair) {
		return telemetry_data_time_pair.second.data.bandwidth_cap == bandwidth_cap;
	});
	ASSERT_FALSE (all_bandwidth_limits_same);
}

TEST (node_telemetry, receive_from_non_listening_channel)
{
	nano::system system;
	auto node = system.add_node ();
	nano::telemetry_ack message (nano::telemetry_data{});
	node->network.process_message (message, node->network.udp_channels.create (node->network.endpoint ()));
	// We have not sent a telemetry_req message to this endpoint, so shouldn't count telemetry_ack received from it.
	ASSERT_EQ (node->telemetry.telemetry_data_size (), 0);
}

TEST (node_telemetry, over_udp)
{
	nano::system system;
	nano::node_flags node_flags;
	node_flags.disable_tcp_realtime = true;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	wait_peer_connections (system);

	std::atomic<bool> done{ false };
	std::unordered_map<nano::endpoint, nano::telemetry_data_time_pair> all_telemetry_data_time_pairs;
	node_client->telemetry.get_metrics_peers_async ([&done, &all_telemetry_data_time_pairs](nano::telemetry_data_responses const & responses_a) {
		ASSERT_TRUE (responses_a.all_received);
		all_telemetry_data_time_pairs = responses_a.telemetry_data_time_pairs;
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	ASSERT_EQ (all_telemetry_data_time_pairs.size (), 1);
	compare_default_test_result_data (all_telemetry_data_time_pairs.begin ()->second.data, *node_server);

	// Check channels are indeed udp
	ASSERT_EQ (1, node_client->network.size ());
	auto list1 (node_client->network.list (2));
	ASSERT_EQ (node_server->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (nano::transport::transport_type::udp, list1[0]->get_type ());
	ASSERT_EQ (1, node_server->network.size ());
	auto list2 (node_server->network.list (2));
	ASSERT_EQ (node_client->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (nano::transport::transport_type::udp, list2[0]->get_type ());
}

namespace nano
{
TEST (node_telemetry, single_request)
{
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	wait_peer_connections (system);

	// Request telemetry metrics
	auto channel = node_client->network.find_channel (node_server->network.endpoint ());
	nano::telemetry_data_time_pair telemetry_data_time_pair;
	{
		std::atomic<bool> done{ false };

		node_client->telemetry.get_metrics_single_peer_async (channel, [&done, &telemetry_data_time_pair, &channel](nano::telemetry_data_response const & response_a) {
			ASSERT_FALSE (response_a.error);
			ASSERT_EQ (channel->get_endpoint (), response_a.endpoint);
			telemetry_data_time_pair = response_a.telemetry_data_time_pair;
			done = true;
		});

		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}

	// Check the metrics are correct
	compare_default_test_result_data (telemetry_data_time_pair.data, *node_server);

	// Call again straight away. It should use the cache
	{
		std::atomic<bool> done{ false };
		node_client->telemetry.get_metrics_single_peer_async (channel, [&done, &telemetry_data_time_pair](nano::telemetry_data_response const & response_a) {
			ASSERT_EQ (telemetry_data_time_pair, response_a.telemetry_data_time_pair);
			ASSERT_FALSE (response_a.error);
			done = true;
		});

		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}

	// Wait the cache period and check cache is not used
	std::this_thread::sleep_for (nano::telemetry_cache_cutoffs::test);

	std::atomic<bool> done{ false };
	node_client->telemetry.get_metrics_single_peer_async (channel, [&done, &telemetry_data_time_pair](nano::telemetry_data_response const & response_a) {
		ASSERT_NE (telemetry_data_time_pair, response_a.telemetry_data_time_pair);
		ASSERT_FALSE (response_a.error);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}
}

TEST (node_telemetry, single_request_invalid_channel)
{
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	std::atomic<bool> done{ false };
	node_client->telemetry.get_metrics_single_peer_async (nullptr, [&done](nano::telemetry_data_response const & response_a) {
		ASSERT_TRUE (response_a.error);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node_telemetry, blocking_single_and_random)
{
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	wait_peer_connections (system);

	// Request telemetry metrics
	std::atomic<bool> done{ false };
	std::function<void()> call_system_poll;
	std::promise<void> promise;
	call_system_poll = [&call_system_poll, &worker = node_client->worker, &done, &system, &promise]() {
		if (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
			worker.push_task (call_system_poll);
		}
		else
		{
			promise.set_value ();
		}
	};

	// Keep pushing system.polls in another thread (worker), because we will be blocking this thread and unable to do so.
	system.deadline_set (10s);
	node_client->worker.push_task (call_system_poll);

	// Blocking version of get_random_metrics_async
	auto telemetry_data_responses = node_client->telemetry.get_metrics_peers ();
	ASSERT_TRUE (telemetry_data_responses.all_received);
	compare_default_test_result_data (telemetry_data_responses.telemetry_data_time_pairs.begin ()->second.data, *node_server);

	// Now try single request metric
	auto telemetry_data_response = node_client->telemetry.get_metrics_single_peer (node_client->network.find_channel (node_server->network.endpoint ()));
	ASSERT_FALSE (telemetry_data_response.error);
	compare_default_test_result_data (telemetry_data_response.telemetry_data_time_pair.data, *node_server);
	ASSERT_EQ (telemetry_data_response.telemetry_data_time_pair.last_updated, telemetry_data_responses.telemetry_data_time_pairs.begin ()->second.last_updated);

	done = true;
	promise.get_future ().wait ();
}

namespace nano
{
TEST (node_telemetry, multiple_single_request_clearing)
{
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.bandwidth_limit = 100000;
	auto node_server1 = system.add_node (node_config);

	wait_peer_connections (system);

	// Request telemetry metrics
	auto channel = node_client->network.find_channel (node_server->network.endpoint ());

	std::atomic<bool> done{ false };
	std::chrono::steady_clock::time_point last_updated;
	node_client->telemetry.get_metrics_single_peer_async (channel, [&done, &last_updated](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.error);
		last_updated = response_a.telemetry_data_time_pair.last_updated;
		done = true;
	});

	ASSERT_EQ (1, node_client->telemetry.single_requests.size ());
	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	done = false;
	// Make another request to keep the time updated
	system.deadline_set (10s);
	node_client->telemetry.get_metrics_single_peer_async (channel, [&done, last_updated](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.error);
		ASSERT_EQ (last_updated, response_a.telemetry_data_time_pair.last_updated);
		done = true;
	});

	ASSERT_LT (last_updated, node_client->telemetry.single_requests.begin ()->second.last_updated);

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	done = false;
	auto channel1 = node_client->network.find_channel (node_server1->network.endpoint ());
	node_client->telemetry.get_metrics_single_peer_async (channel1, [&done, &last_updated](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.error);
		ASSERT_NE (last_updated, response_a.telemetry_data_time_pair.last_updated);
		last_updated = response_a.telemetry_data_time_pair.last_updated;
		done = true;
	});

	system.deadline_set (10s);

	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	done = false;
	node_client->telemetry.get_metrics_single_peer_async (channel1, [&done, last_updated](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.error);
		ASSERT_EQ (last_updated, response_a.telemetry_data_time_pair.last_updated);
		done = true;
	});

	// single_requests should be removed as no more calls are being back
	system.deadline_set (10s);
	nano::unique_lock<std::mutex> lk (node_client->telemetry.mutex);
	while (!node_client->telemetry.single_requests.empty () || !done)
	{
		lk.unlock ();
		ASSERT_NO_ERROR (system.poll ());
		lk.lock ();
	}
}
}

TEST (node_telemetry, disconnects)
{
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	wait_peer_connections (system);

	// Try and request metrics from a node which is turned off but a channel is not closed yet
	auto channel = node_client->network.find_channel (node_server->network.endpoint ());
	node_server->stop ();
	ASSERT_TRUE (channel);

	std::atomic<bool> done{ false };
	node_client->telemetry.get_metrics_peers_async ([&done](nano::telemetry_data_responses const & responses_a) {
		ASSERT_FALSE (responses_a.all_received);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	done = false;
	node_client->telemetry.get_metrics_single_peer_async (channel, [&done](nano::telemetry_data_response const & response_a) {
		ASSERT_TRUE (response_a.error);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node_telemetry, batch_use_single_request_cache)
{
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	wait_peer_connections (system);

	// Request telemetry metrics
	nano::telemetry_data_time_pair telemetry_data_time_pair;
	{
		std::atomic<bool> done{ false };
		auto channel = node_client->network.find_channel (node_server->network.endpoint ());
		node_client->telemetry.get_metrics_single_peer_async (channel, [&done, &telemetry_data_time_pair](nano::telemetry_data_response const & response_a) {
			telemetry_data_time_pair = response_a.telemetry_data_time_pair;
			done = true;
		});

		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}

	{
		std::atomic<bool> done{ false };
		node_client->telemetry.get_metrics_peers_async ([&done, &telemetry_data_time_pair](nano::telemetry_data_responses const & responses_a) {
			ASSERT_TRUE (responses_a.all_received);
			ASSERT_EQ (telemetry_data_time_pair, responses_a.telemetry_data_time_pairs.begin ()->second);
			done = true;
		});

		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}

	// Confirm only 1 request was made
	ASSERT_EQ (1, node_client->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_ack, nano::stat::dir::in));
	ASSERT_EQ (0, node_client->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));
	ASSERT_EQ (1, node_client->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::out));
	ASSERT_EQ (0, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_ack, nano::stat::dir::in));
	ASSERT_EQ (1, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));
	ASSERT_EQ (0, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::out));

	// Wait until there is something pending
	system.deadline_set (10s);
	while (node_client->telemetry.finished_single_requests_size () == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	system.deadline_set (10s);
	std::atomic<bool> done{ false };
	node_client->telemetry.get_metrics_peers_async ([&done, &telemetry_data_time_pair](nano::telemetry_data_responses const & responses_a) {
		ASSERT_EQ (1, responses_a.telemetry_data_time_pairs.size ());
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	ASSERT_EQ (0, node_client->telemetry.finished_single_requests_size ());
	ASSERT_EQ (2, node_client->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_ack, nano::stat::dir::in));
	ASSERT_EQ (0, node_client->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));
	ASSERT_EQ (2, node_client->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::out));
	ASSERT_EQ (0, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_ack, nano::stat::dir::in));
	ASSERT_EQ (2, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));
	ASSERT_EQ (0, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::out));
}

TEST (node_telemetry, single_request_use_batch_cache)
{
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	wait_peer_connections (system);

	// Request batched metric first
	std::unordered_map<nano::endpoint, nano::telemetry_data_time_pair> all_telemetry_data_time_pairs;
	{
		std::atomic<bool> done{ false };
		node_client->telemetry.get_metrics_peers_async ([&done, &all_telemetry_data_time_pairs](nano::telemetry_data_responses const & responses_a) {
			ASSERT_TRUE (responses_a.all_received);
			ASSERT_EQ (1, responses_a.telemetry_data_time_pairs.size ());
			all_telemetry_data_time_pairs = responses_a.telemetry_data_time_pairs;
			done = true;
		});

		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}

	std::atomic<bool> done{ false };
	auto channel = node_client->network.find_channel (node_server->network.endpoint ());
	node_client->telemetry.get_metrics_single_peer_async (channel, [&done, &all_telemetry_data_time_pairs](nano::telemetry_data_response const & response_a) {
		ASSERT_EQ (all_telemetry_data_time_pairs.begin ()->second, response_a.telemetry_data_time_pair);
		ASSERT_FALSE (response_a.error);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Confirm only 1 request was made
	ASSERT_EQ (1, node_client->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_ack, nano::stat::dir::in));
	ASSERT_EQ (0, node_client->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));
	ASSERT_EQ (1, node_client->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::out));
	ASSERT_EQ (0, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_ack, nano::stat::dir::in));
	ASSERT_EQ (1, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));
	ASSERT_EQ (0, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::out));
}

TEST (node_telemetry, dos_tcp)
{
	// Confirm that telemetry_reqs are not processed
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	wait_peer_connections (system);

	nano::telemetry_req message;
	auto channel = node_client->network.tcp_channels.find_channel (nano::transport::map_endpoint_to_tcp (node_server->network.endpoint ()));
	channel->send (message, [](boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
	});

	system.deadline_set (10s);
	while (1 != node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in))
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	auto orig = std::chrono::steady_clock::now ();
	for (int i = 0; i < 10; ++i)
	{
		channel->send (message, [](boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
		});
	}

	system.deadline_set (10s);
	while ((nano::telemetry_cache_cutoffs::test + orig) > std::chrono::steady_clock::now ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Should process no more telemetry_req messages
	ASSERT_EQ (1, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));

	// Now spam messages waiting for it to be processed
	while (node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in) == 1)
	{
		channel->send (message);
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node_telemetry, dos_udp)
{
	// Confirm that telemetry_reqs are not processed
	nano::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	wait_peer_connections (system);

	nano::telemetry_req message;
	auto channel (node_server->network.udp_channels.create (node_server->network.endpoint ()));
	channel->send (message, [](boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
	});

	system.deadline_set (20s);
	while (1 != node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in))
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	auto orig = std::chrono::steady_clock::now ();
	for (int i = 0; i < 10; ++i)
	{
		channel->send (message, [](boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
		});
	}

	system.deadline_set (20s);
	while ((nano::telemetry_cache_cutoffs::test + orig) > std::chrono::steady_clock::now ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Should process no more telemetry_req messages
	ASSERT_EQ (1, node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in));

	// Now spam messages waiting for it to be processed
	system.deadline_set (20s);
	while (node_server->stats.count (nano::stat::type::message, nano::stat::detail::telemetry_req, nano::stat::dir::in) == 1)
	{
		channel->send (message);
		ASSERT_NO_ERROR (system.poll ());
	}
}

namespace
{
void wait_peer_connections (nano::system & system_a)
{
	system_a.deadline_set (10s);
	auto peer_count = 0;
	auto num_nodes = system_a.nodes.size ();
	while (peer_count != num_nodes * (num_nodes - 1))
	{
		ASSERT_NO_ERROR (system_a.poll ());
		peer_count = std::accumulate (system_a.nodes.cbegin (), system_a.nodes.cend (), 0, [](auto total, auto const & node) {
			auto transaction = node->store.tx_begin_read ();
			return total += node->store.peer_count (transaction);
		});
	}
}

void compare_default_test_result_data (nano::telemetry_data & telemetry_data_a, nano::node const & node_server_a)
{
	ASSERT_EQ (telemetry_data_a.block_count, 1);
	ASSERT_EQ (telemetry_data_a.cemented_count, 1);
	ASSERT_EQ (telemetry_data_a.bandwidth_cap, node_server_a.config.bandwidth_limit);
	ASSERT_EQ (telemetry_data_a.peer_count, 1);
	ASSERT_EQ (telemetry_data_a.protocol_version_number, node_server_a.network_params.protocol.telemetry_protocol_version_min);
	ASSERT_EQ (telemetry_data_a.unchecked_count, 0);
	ASSERT_EQ (telemetry_data_a.account_count, 1);
	ASSERT_EQ (telemetry_data_a.vendor_version, nano::get_major_node_version ());
	ASSERT_LT (telemetry_data_a.uptime, 100);
	ASSERT_EQ (telemetry_data_a.genesis_block, nano::genesis ().hash ());
}
}
