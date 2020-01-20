#include <nano/core_test/testutil.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/node/request_aggregator.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (request_aggregator, one)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	nano::genesis genesis;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::Gxrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	system.deadline_set (3s);
	while (!node.aggregator.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Not yet in the ledger, should be ignored
	ASSERT_EQ (1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_ignored));
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	// Generated votes are created after the pool is removed from the aggregator, so a simple check on empty () is not enough
	system.deadline_set (3s);
	while (node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node.aggregator.empty ());
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// Already cached
	system.deadline_set (3s);
	while (!node.aggregator.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (3, node.stats.count (nano::stat::type::requests, nano::stat::detail::all));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_ignored));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

TEST (request_aggregator, one_update)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	nano::genesis genesis;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::Gxrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 2 * nano::Gxrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *node.work_generate_blocking (send1->hash ())));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send2).code);
	node.aggregator.add (channel, request);
	request.clear ();
	request.emplace_back (send2->hash (), send2->root ());
	// Update the pool of requests with another hash
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	// Generated votes are created after the pool is removed from the aggregator, so a simple check on empty () is not enough
	system.deadline_set (3s);
	while (node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node.aggregator.empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::requests, nano::stat::detail::all));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_ignored));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

TEST (request_aggregator, two)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	nano::genesis genesis;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::Gxrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	auto send2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, send1->hash (), nano::test_genesis_key.pub, nano::genesis_amount - 2 * nano::Gxrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *node.work_generate_blocking (send1->hash ())));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	request.emplace_back (send2->hash (), send2->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	// Process both blocks
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send2).code);
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// One vote should be generated for both blocks
	// Generated votes are created after the pool is removed from the aggregator, so a simple check on empty () is not enough
	system.deadline_set (3s);
	while (node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node.aggregator.empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated));
	// The same request should now send the cached vote
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	system.deadline_set (3s);
	while (!node.aggregator.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (2, node.stats.count (nano::stat::type::requests, nano::stat::detail::all));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_ignored));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
	// Make sure the cached vote is for both hashes
	auto vote1 (node.votes_cache.find (send1->hash ()));
	auto vote2 (node.votes_cache.find (send2->hash ()));
	ASSERT_EQ (1, vote1.size ());
	ASSERT_EQ (1, vote2.size ());
	ASSERT_EQ (vote1.front (), vote2.front ());
}

TEST (request_aggregator, two_endpoints)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 (*system.add_node (node_config));
	node_config.peering_port = nano::get_available_port ();
	auto & node2 (*system.add_node (node_config));
	nano::genesis genesis;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::Gxrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *node1.work_generate_blocking (genesis.hash ())));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	ASSERT_EQ (nano::process_result::progress, node1.ledger.process (node1.store.tx_begin_write (), *send1).code);
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	auto channel2 (node2.network.udp_channels.create (node2.network.endpoint ()));
	ASSERT_NE (nano::transport::map_endpoint_to_v6 (channel1->get_endpoint ()), nano::transport::map_endpoint_to_v6 (channel2->get_endpoint ()));
	// Use the aggregator from node1 only, making requests from both nodes
	node1.aggregator.add (channel1, request);
	node1.aggregator.add (channel2, request);
	ASSERT_EQ (2, node1.aggregator.size ());
	system.deadline_set (3s);
	// For the first request it generates the vote, for the second it uses the generated vote
	while (!node1.aggregator.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (2, node1.stats.count (nano::stat::type::requests, nano::stat::detail::all));
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_ignored));
	ASSERT_EQ (1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated));
	ASSERT_EQ (1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached));
}

TEST (request_aggregator, split)
{
	constexpr size_t max_vbh = nano::network::confirm_ack_hashes_max;
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	nano::genesis genesis;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	auto previous = genesis.hash ();
	// Add max_vbh + 1 blocks and request votes for them
	for (size_t i (0); i <= max_vbh; ++i)
	{
		nano::block_builder builder;
		auto block = builder
		             .state ()
		             .account (nano::test_genesis_key.pub)
		             .previous (previous)
		             .representative (nano::test_genesis_key.pub)
		             .balance (nano::genesis_amount - (i + 1))
		             .link (nano::test_genesis_key.pub)
		             .sign (nano::test_genesis_key.prv, nano::test_genesis_key.pub)
		             .work (*system.work.generate (previous))
		             .build ();
		previous = block->hash ();
		ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *block).code);
		request.emplace_back (block->hash (), block->root ());
	}
	ASSERT_EQ (max_vbh + 1, request.size ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	// Generated votes are created after the pool is removed from the aggregator, so a simple check on empty () is not enough
	system.deadline_set (3s);
	while (node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated) < 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node.aggregator.empty ());
	// Two votes were sent, the first one for 12 hashes and the second one for 1 hash
	ASSERT_EQ (2, node.stats.count (nano::stat::type::requests, nano::stat::detail::all));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_ignored));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
	auto transaction (node.store.tx_begin_read ());
	auto pre_last_hash (node.store.block_get (transaction, previous)->previous ());
	auto vote1 (node.votes_cache.find (pre_last_hash));
	ASSERT_EQ (1, vote1.size ());
	ASSERT_EQ (max_vbh, vote1[0]->blocks.size ());
	auto vote2 (node.votes_cache.find (previous));
	ASSERT_EQ (1, vote2.size ());
	ASSERT_EQ (1, vote2[0]->blocks.size ());
}

TEST (request_aggregator, channel_lifetime)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	nano::genesis genesis;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::Gxrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	{
		// The aggregator should extend the lifetime of the channel
		auto channel (node.network.udp_channels.create (node.network.endpoint ()));
		node.aggregator.add (channel, request);
	}
	ASSERT_EQ (1, node.aggregator.size ());
	system.deadline_set (3s);
	while (node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (request_aggregator, channel_update)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	nano::genesis genesis;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::Gxrb_ratio, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	std::weak_ptr<nano::transport::channel> channel1_w;
	{
		auto channel1 (node.network.udp_channels.create (node.network.endpoint ()));
		channel1_w = channel1;
		node.aggregator.add (channel1, request);
		auto channel2 (node.network.udp_channels.create (node.network.endpoint ()));
		// The aggregator then hold channel2 and drop channel1
		node.aggregator.add (channel2, request);
	}
	// Both requests were for the same endpoint, so only one pool should exist
	ASSERT_EQ (1, node.aggregator.size ());
	// channel1 is not being held anymore
	ASSERT_EQ (nullptr, channel1_w.lock ());
	system.deadline_set (3s);
	while (node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}
