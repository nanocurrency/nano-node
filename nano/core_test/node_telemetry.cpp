#include <nano/core_test/testutil.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

namespace
{
void wait_peer_connections (nano::system & system_a);
void compare_default_test_result_data (nano::telemetry_data const & telemetry_data_a, nano::node const & node_server_a);
}

TEST (node_telemetry, consolidate_data)
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
	data.patch_version = 4;

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
	data1.maker = 2;

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

	std::vector<nano::telemetry_data> all_data{ data, data1, data2 };

	auto consolidated_telemetry_data = nano::telemetry_data::consolidate (all_data);
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
	ASSERT_FALSE (consolidated_telemetry_data.minor_version.is_initialized ());
	ASSERT_FALSE (consolidated_telemetry_data.patch_version.is_initialized ());
	ASSERT_FALSE (consolidated_telemetry_data.maker.is_initialized ());

	// Modify the metrics which may be either the mode or averages to ensure all are tested.
	all_data[2].bandwidth_cap = 53;
	all_data[2].protocol_version = 13;
	all_data[2].genesis_block = nano::block_hash (3);
	all_data[2].major_version = 10;
	all_data[2].minor_version = 2;
	all_data[2].patch_version = 3;
	all_data[2].maker = 2;

	auto consolidated_telemetry_data1 = nano::telemetry_data::consolidate (all_data);
	ASSERT_EQ (consolidated_telemetry_data1.major_version, 10);
	ASSERT_EQ (*consolidated_telemetry_data1.minor_version, 2);
	ASSERT_EQ (*consolidated_telemetry_data1.patch_version, 3);
	ASSERT_EQ (*consolidated_telemetry_data1.maker, 2);
	ASSERT_TRUE (consolidated_telemetry_data1.protocol_version == 11 || consolidated_telemetry_data1.protocol_version == 12 || consolidated_telemetry_data1.protocol_version == 13);
	ASSERT_EQ (consolidated_telemetry_data1.bandwidth_cap, 51);
	ASSERT_EQ (consolidated_telemetry_data1.genesis_block, nano::block_hash (3));

	// Test equality operator
	ASSERT_FALSE (consolidated_telemetry_data == consolidated_telemetry_data1);
	ASSERT_EQ (consolidated_telemetry_data, consolidated_telemetry_data);
}

TEST (node_telemetry, consolidate_data_optional_data)
{
	nano::telemetry_data data;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 4;
	data.maker = 2;

	nano::telemetry_data missing_minor;
	missing_minor.major_version = 20;
	missing_minor.patch_version = 4;

	nano::telemetry_data missing_all_optional;

	std::vector<nano::telemetry_data> all_data{ data, data, missing_minor, missing_all_optional };
	auto consolidated_telemetry_data = nano::telemetry_data::consolidate (all_data);
	ASSERT_EQ (consolidated_telemetry_data.major_version, 20);
	ASSERT_EQ (*consolidated_telemetry_data.minor_version, 1);
	ASSERT_EQ (*consolidated_telemetry_data.patch_version, 4);
	ASSERT_EQ (*consolidated_telemetry_data.maker, 2);
}

TEST (node_telemetry, serialize_deserialize_json_optional)
{
	nano::telemetry_data data;
	data.minor_version = 1;
	data.patch_version = 4;
	data.maker = 2;

	nano::jsonconfig config;
	data.serialize_json (config);

	uint8_t val;
	ASSERT_FALSE (config.get ("minor_version", val).get_error ());
	ASSERT_EQ (val, 1);
	ASSERT_FALSE (config.get ("patch_version", val).get_error ());
	ASSERT_EQ (val, 4);
	ASSERT_FALSE (config.get ("maker", val).get_error ());
	ASSERT_EQ (val, 2);

	nano::telemetry_data data1;
	data1.deserialize_json (config);
	ASSERT_EQ (*data1.minor_version, 1);
	ASSERT_EQ (*data1.patch_version, 4);
	ASSERT_EQ (*data1.maker, 2);

	nano::telemetry_data no_optional_data;
	nano::jsonconfig config1;
	no_optional_data.serialize_json (config1);
	ASSERT_FALSE (config1.get_optional<uint8_t> ("minor_version").is_initialized ());
	ASSERT_FALSE (config1.get_optional<uint8_t> ("patch_version").is_initialized ());
	ASSERT_FALSE (config1.get_optional<uint8_t> ("maker").is_initialized ());

	nano::telemetry_data no_optional_data1;
	no_optional_data1.deserialize_json (config1);
	ASSERT_FALSE (no_optional_data1.minor_version.is_initialized ());
	ASSERT_FALSE (no_optional_data1.patch_version.is_initialized ());
	ASSERT_FALSE (no_optional_data1.maker.is_initialized ());
}

TEST (node_telemetry, no_peers)
{
	nano::system system (1);

	std::atomic<bool> done{ false };
	system.nodes[0]->telemetry.get_metrics_random_peers_async ([&done](nano::telemetry_data_responses const & responses_a) {
		ASSERT_TRUE (responses_a.data.empty ());
		ASSERT_FALSE (responses_a.all_received);
		ASSERT_FALSE (responses_a.is_cached);
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
	std::vector<nano::telemetry_data> all_telemetry_data;
	{
		std::atomic<bool> done{ false };
		node_client->telemetry.get_metrics_random_peers_async ([&done, &all_telemetry_data](nano::telemetry_data_responses const & responses_a) {
			ASSERT_FALSE (responses_a.is_cached);
			ASSERT_TRUE (responses_a.all_received);
			all_telemetry_data = responses_a.data;
			done = true;
		});

		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}

	// Check the metrics are correct
	ASSERT_EQ (all_telemetry_data.size (), 1);
	auto & telemetry_data = all_telemetry_data.front ();
	compare_default_test_result_data (telemetry_data, *node_server);

	// Call again straight away. It should use the cache
	{
		std::atomic<bool> done{ false };
		node_client->telemetry.get_metrics_random_peers_async ([&done, &telemetry_data](nano::telemetry_data_responses const & responses_a) {
			ASSERT_EQ (telemetry_data, responses_a.data.front ());
			ASSERT_TRUE (responses_a.is_cached);
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
	std::this_thread::sleep_for (nano::telemetry_impl::cache_cutoff);

	std::atomic<bool> done{ false };
	node_client->telemetry.get_metrics_random_peers_async ([&done, &telemetry_data](nano::telemetry_data_responses const & responses_a) {
		ASSERT_FALSE (responses_a.is_cached);
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
	std::vector<nano::telemetry_data> all_telemetry_data;
	node_client->telemetry.get_metrics_random_peers_async ([&done, &all_telemetry_data](nano::telemetry_data_responses const & responses_a) {
		ASSERT_FALSE (responses_a.is_cached);
		ASSERT_TRUE (responses_a.all_received);
		all_telemetry_data = responses_a.data;
		done = true;
	});

	system.deadline_set (20s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Check the metrics
	nano::network_params params;
	for (auto & data : all_telemetry_data)
	{
		ASSERT_EQ (data.unchecked_count, 0);
		ASSERT_EQ (data.cemented_count, 1);
		ASSERT_LE (data.peer_count, 9);
		ASSERT_EQ (data.account_count, 1);
		ASSERT_TRUE (data.block_count == 2);
		ASSERT_EQ (data.protocol_version, params.protocol.telemetry_protocol_version_min);
		ASSERT_GE (data.bandwidth_cap, 100000);
		ASSERT_LT (data.bandwidth_cap, 100000 + system.nodes.size ());
		ASSERT_EQ (data.major_version, nano::get_major_node_version ());
		ASSERT_EQ (*data.minor_version, nano::get_minor_node_version ());
		ASSERT_EQ (*data.patch_version, nano::get_patch_node_version ());
		ASSERT_EQ (*data.maker, 0);
		ASSERT_LT (data.uptime, 100);
		ASSERT_EQ (data.genesis_block, genesis.hash ());
	}

	// We gave some nodes different bandwidth caps, confirm they are not all the time
	auto all_bandwidth_limits_same = std::all_of (all_telemetry_data.begin () + 1, all_telemetry_data.end (), [bandwidth_cap = all_telemetry_data[0].bandwidth_cap](auto & telemetry) {
		return telemetry.bandwidth_cap == bandwidth_cap;
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
	std::vector<nano::telemetry_data> all_telemetry_data;
	node_client->telemetry.get_metrics_random_peers_async ([&done, &all_telemetry_data](nano::telemetry_data_responses const & responses_a) {
		ASSERT_FALSE (responses_a.is_cached);
		ASSERT_TRUE (responses_a.all_received);
		all_telemetry_data = responses_a.data;
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	ASSERT_EQ (all_telemetry_data.size (), 1);
	compare_default_test_result_data (all_telemetry_data.front (), *node_server);

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

TEST (node_telemetry, simultaneous_random_requests)
{
	const auto num_nodes = 4;
	nano::system system (num_nodes);

	// Wait until peers are stored as they are done in the background
	wait_peer_connections (system);

	std::vector<std::thread> threads;
	const auto num_threads = 4;

	std::atomic<bool> done{ false };
	class Data
	{
	public:
		std::atomic<bool> awaiting_cache{ false };
		std::atomic<bool> keep_requesting_metrics{ true };
		std::shared_ptr<nano::node> node;
	};

	std::array<Data, num_nodes> all_data{};
	for (auto i = 0; i < num_nodes; ++i)
	{
		all_data[i].node = system.nodes[i];
	}

	std::atomic<uint64_t> count{ 0 };
	std::promise<void> promise;
	std::shared_future<void> shared_future (promise.get_future ());

	// Create a few threads where each node sends out telemetry request messages to all other nodes continuously, until the cache it reached and subsequently expired.
	// The test waits until all telemetry_ack messages have been received.
	for (int i = 0; i < num_threads; ++i)
	{
		threads.emplace_back ([&all_data, &done, &count, &promise, &shared_future]() {
			while (std::any_of (all_data.cbegin (), all_data.cend (), [](auto const & data) { return data.keep_requesting_metrics.load (); }))
			{
				for (auto & data : all_data)
				{
					// Keep calling get_metrics_async until the cache has been saved and then become outdated (after a certain period of time) for each node
					if (data.keep_requesting_metrics)
					{
						++count;

						data.node->telemetry.get_metrics_random_peers_async ([&promise, &done, &data, &all_data, &count](nano::telemetry_data_responses const & responses_a) {
							if (data.awaiting_cache && !responses_a.is_cached)
							{
								data.keep_requesting_metrics = false;
							}
							if (responses_a.is_cached)
							{
								data.awaiting_cache = true;
							}
							if (--count == 0 && std::all_of (all_data.begin (), all_data.end (), [](auto const & data) { return !data.keep_requesting_metrics; }))
							{
								done = true;
								promise.set_value ();
							}
						});
					}
					std::this_thread::sleep_for (1ms);
				}
			}

			ASSERT_EQ (count, 0);
			shared_future.wait ();
		});
	}

	system.deadline_set (20s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	for (auto & thread : threads)
	{
		thread.join ();
	}
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
	nano::telemetry_data telemetry_data;
	{
		std::atomic<bool> done{ false };

		node_client->telemetry.get_metrics_single_peer_async (channel, [&done, &telemetry_data](nano::telemetry_data_response const & response_a) {
			ASSERT_FALSE (response_a.is_cached);
			ASSERT_FALSE (response_a.error);
			telemetry_data = response_a.data;
			done = true;
		});

		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}

	// Check the metrics are correct
	compare_default_test_result_data (telemetry_data, *node_server);

	// Call again straight away. It should use the cache
	{
		std::atomic<bool> done{ false };
		node_client->telemetry.get_metrics_single_peer_async (channel, [&done, &telemetry_data](nano::telemetry_data_response const & response_a) {
			ASSERT_EQ (telemetry_data, response_a.data);
			ASSERT_TRUE (response_a.is_cached);
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
	std::this_thread::sleep_for (nano::telemetry_impl::cache_cutoff);

	std::atomic<bool> done{ false };
	node_client->telemetry.get_metrics_single_peer_async (channel, [&done, &telemetry_data](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.is_cached);
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

TEST (node_telemetry, simultaneous_single_and_random_requests)
{
	const auto num_nodes = 4;
	nano::system system (num_nodes);

	wait_peer_connections (system);

	std::vector<std::thread> threads;
	const auto num_threads = 4;

	class data
	{
	public:
		std::atomic<bool> awaiting_cache{ false };
		std::atomic<bool> keep_requesting_metrics{ true };
		std::shared_ptr<nano::node> node;
	};

	std::array<data, num_nodes> node_data_single{};
	std::array<data, num_nodes> node_data_random{};
	for (auto i = 0; i < num_nodes; ++i)
	{
		node_data_single[i].node = system.nodes[i];
		node_data_random[i].node = system.nodes[i];
	}

	class shared_data
	{
	public:
		std::atomic<bool> done{ false };
		std::atomic<uint64_t> count{ 0 };
		std::promise<void> promise;
		std::shared_future<void> shared_future{ promise.get_future () };
	};

	shared_data shared_data_single;
	shared_data shared_data_random;

	// Create a few threads where each node sends out telemetry request messages to all other nodes continuously, until the cache it reached and subsequently expired.
	// The test waits until all telemetry_ack messages have been received.
	for (int i = 0; i < num_threads; ++i)
	{
		threads.emplace_back ([&node_data_single, &node_data_random, &shared_data_single, &shared_data_random]() {
			auto func = [](auto & all_node_data_a, shared_data & shared_data_a) {
				while (std::any_of (all_node_data_a.cbegin (), all_node_data_a.cend (), [](auto const & data) { return data.keep_requesting_metrics.load (); }))
				{
					for (auto & data : all_node_data_a)
					{
						// Keep calling get_metrics_async until the cache has been saved and then become outdated (after a certain period of time) for each node
						if (data.keep_requesting_metrics)
						{
							++shared_data_a.count;

							data.node->telemetry.get_metrics_random_peers_async ([& shared_data = shared_data_a, &data, &all_node_data = all_node_data_a](nano::telemetry_data_responses const & responses_a) {
								if (data.awaiting_cache && !responses_a.is_cached)
								{
									data.keep_requesting_metrics = false;
								}
								if (responses_a.is_cached)
								{
									data.awaiting_cache = true;
								}
								if (--shared_data.count == 0 && std::all_of (all_node_data.begin (), all_node_data.end (), [](auto const & data) { return !data.keep_requesting_metrics; }))
								{
									shared_data.done = true;
									shared_data.promise.set_value ();
								}
							});
						}
						std::this_thread::sleep_for (1ms);
					}
				}

				ASSERT_EQ (shared_data_a.count, 0);
				shared_data_a.shared_future.wait ();
			};

			func (node_data_single, shared_data_single);
			func (node_data_random, shared_data_random);
		});
	}

	system.deadline_set (20s);
	while (!shared_data_single.done || !shared_data_random.done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	for (auto & thread : threads)
	{
		thread.join ();
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
	auto telemetry_data_responses = node_client->telemetry.get_metrics_random_peers ();
	ASSERT_FALSE (telemetry_data_responses.is_cached);
	ASSERT_TRUE (telemetry_data_responses.all_received);
	compare_default_test_result_data (telemetry_data_responses.data.front (), *node_server);

	// Now try single request metric
	auto telemetry_data_response = node_client->telemetry.get_metrics_single_peer (node_client->network.find_channel (node_server->network.endpoint ()));
	ASSERT_FALSE (telemetry_data_response.is_cached);
	ASSERT_FALSE (telemetry_data_response.error);
	compare_default_test_result_data (telemetry_data_response.data, *node_server);

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
	node_client->telemetry.get_metrics_single_peer_async (channel, [&done](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.error);
		ASSERT_FALSE (response_a.is_cached);
		done = true;
	});

	ASSERT_EQ (1, node_client->telemetry.single_requests.size ());
	auto last_updated = node_client->telemetry.single_requests.begin ()->second.last_updated;

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	done = false;
	// Make another request to keep
	system.deadline_set (10s);
	node_client->telemetry.get_metrics_single_peer_async (channel, [&done](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.error);
		ASSERT_TRUE (response_a.is_cached);
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
	node_client->telemetry.get_metrics_single_peer_async (channel1, [&done](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.error);
		ASSERT_FALSE (response_a.is_cached);
		done = true;
	});

	system.deadline_set (10s);

	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	done = false;
	node_client->telemetry.get_metrics_single_peer_async (channel1, [&done](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.error);
		ASSERT_TRUE (response_a.is_cached);
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
	node_client->telemetry.get_metrics_random_peers_async ([&done](nano::telemetry_data_responses const & responses_a) {
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

TEST (node_telemetry, disable_metrics_single)
{
	nano::system system (1);
	auto node_client = system.nodes.front ();
	nano::node_flags node_flags;
	node_flags.disable_providing_telemetry_metrics = true;
	auto node_server = system.add_node (node_flags);

	wait_peer_connections (system);

	// Try and request metrics from a node which is turned off but a channel is not closed yet
	auto channel = node_client->network.find_channel (node_server->network.endpoint ());
	ASSERT_TRUE (channel);

	std::atomic<bool> done{ false };
	node_client->telemetry.get_metrics_single_peer_async (channel, [&done](nano::telemetry_data_response const & response_a) {
		ASSERT_TRUE (response_a.error);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// It should still be able to receive metrics though
	done = false;
	auto channel1 = node_server->network.find_channel (node_client->network.endpoint ());
	node_server->telemetry.get_metrics_single_peer_async (channel1, [&done, node_server](nano::telemetry_data_response const & response_a) {
		ASSERT_FALSE (response_a.error);
		compare_default_test_result_data (response_a.data, *node_server);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node_telemetry, disable_metrics_batch)
{
	nano::system system (1);
	auto node_client = system.nodes.front ();
	nano::node_flags node_flags;
	node_flags.disable_providing_telemetry_metrics = true;
	auto node_server = system.add_node (node_flags);

	wait_peer_connections (system);

	// Try and request metrics from a node which is turned off but a channel is not closed yet
	auto channel = node_client->network.find_channel (node_server->network.endpoint ());
	ASSERT_TRUE (channel);

	std::atomic<bool> done{ false };
	node_client->telemetry.get_metrics_random_peers_async ([&done](nano::telemetry_data_responses const & responses_a) {
		ASSERT_FALSE (responses_a.all_received);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// It should still be able to receive metrics though
	done = false;
	node_server->telemetry.get_metrics_random_peers_async ([&done, node_server](nano::telemetry_data_responses const & responses_a) {
		ASSERT_TRUE (responses_a.all_received);
		compare_default_test_result_data (responses_a.data.front (), *node_server);
		done = true;
	});

	system.deadline_set (10s);
	while (!done)
	{
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

void compare_default_test_result_data (nano::telemetry_data const & telemetry_data_a, nano::node const & node_server_a)
{
	ASSERT_EQ (telemetry_data_a.block_count, 1);
	ASSERT_EQ (telemetry_data_a.cemented_count, 1);
	ASSERT_EQ (telemetry_data_a.bandwidth_cap, node_server_a.config.bandwidth_limit);
	ASSERT_EQ (telemetry_data_a.peer_count, 1);
	ASSERT_EQ (telemetry_data_a.protocol_version, node_server_a.network_params.protocol.telemetry_protocol_version_min);
	ASSERT_EQ (telemetry_data_a.unchecked_count, 0);
	ASSERT_EQ (telemetry_data_a.account_count, 1);
	ASSERT_EQ (telemetry_data_a.major_version, nano::get_major_node_version ());
	ASSERT_EQ (*telemetry_data_a.minor_version, nano::get_minor_node_version ());
	ASSERT_EQ (*telemetry_data_a.patch_version, nano::get_patch_node_version ());
	ASSERT_EQ (*telemetry_data_a.maker, 0);
	ASSERT_LT (telemetry_data_a.uptime, 100);
	ASSERT_EQ (telemetry_data_a.genesis_block, nano::genesis ().hash ());
}
}
