#include <nano/lib/jsonconfig.hpp>
#include <nano/node/request_aggregator.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (request_aggregator, one)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build_shared ();
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto client = std::make_shared<nano::client_socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client);
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	// Not yet in the ledger
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// Already cached
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY (3s, 3 == node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

TEST (request_aggregator, one_update)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key1;
	auto send1 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.confirmation_height_processor.add (send1);
	ASSERT_TIMELY (5s, node.ledger.block_confirmed (node.store.tx_begin_read (), send1->hash ()));
	auto send2 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send2).code);
	auto receive1 = nano::state_block_builder ()
					.account (key1.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::Gxrb_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *receive1).code);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send2->hash (), send2->root ());
	auto client = std::make_shared<nano::client_socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client);
	node.aggregator.add (dummy_channel, request);
	request.clear ();
	request.emplace_back (receive1->hash (), receive1->root ());
	// Update the pool of requests with another hash
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes))
	ASSERT_TRUE (node.aggregator.empty ());
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	size_t count = 0;
	ASSERT_TIMELY (3s, 1 == (count = node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes)));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

TEST (request_aggregator, two)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key1;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.confirmation_height_processor.add (send1);
	ASSERT_TIMELY (5s, node.ledger.block_confirmed (node.store.tx_begin_read (), send1->hash ()));
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build_shared ();
	auto receive1 = builder.make_block ()
					.account (key1.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.balance (1)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send2).code);
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *receive1).code);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send2->hash (), send2->root ());
	request.emplace_back (receive1->hash (), receive1->root ());
	auto client = std::make_shared<nano::client_socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client);
	// Process both blocks
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// One vote should be generated for both blocks
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// The same request should now send the cached vote
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
	// Make sure the cached vote is for both hashes
	auto vote1 (node.history.votes (send2->root (), send2->hash ()));
	auto vote2 (node.history.votes (receive1->root (), receive1->hash ()));
	ASSERT_EQ (1, vote1.size ());
	ASSERT_EQ (1, vote2.size ());
	ASSERT_EQ (vote1.front (), vote2.front ());
}

// TODO: fix this test
TEST (request_aggregator, two_endpoints)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	nano::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node1 (*system.add_node (node_config, node_flags));
	node_config.peering_port = nano::test::get_available_port ();
	auto & node2 (*system.add_node (node_config, node_flags));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build_shared ();
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	ASSERT_EQ (nano::process_result::progress, node1.ledger.process (node1.store.tx_begin_write (), *send1).code);

	// Simulate connections only for the remote endpoint fields to be valid.
	boost::asio::ip::tcp::endpoint listen_endpoint1{ boost::asio::ip::address_v6::any (), nano::get_available_port () };
	boost::asio::ip::tcp::endpoint listen_endpoint2{ boost::asio::ip::address_v6::any (), nano::get_available_port () };
	auto server_socket1 = std::make_shared<nano::server_socket> (node2, listen_endpoint1, 1);
	{
		boost::system::error_code ec;
		server_socket1->start (ec);
		ASSERT_FALSE (ec);
	}
	auto server_socket2 = std::make_shared<nano::server_socket> (node1, listen_endpoint2, 1);
	{
		boost::system::error_code ec;
		server_socket2->start (ec);
		ASSERT_FALSE (ec);
	}
	std::shared_ptr<nano::socket> server1_sockets;
	std::shared_ptr<nano::socket> server2_sockets;
	{
		server_socket1->on_connection ([&server1_sockets] (std::shared_ptr<nano::socket> const & new_connection, boost::system::error_code const & ec_a) {
			server1_sockets = new_connection;
			return true;
		});
		server_socket2->on_connection ([&server2_sockets] (std::shared_ptr<nano::socket> const & new_connection, boost::system::error_code const & ec_a) {
			server2_sockets = new_connection;
			return true;
		});
	}
	// client side connection tracking, needed to know the connections are ok
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	auto client1 = std::make_shared<nano::client_socket> (node1);
	client1->async_connect (boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::loopback (), listen_endpoint1.port () }, connect_handler);
	auto client2 = std::make_shared<nano::client_socket> (node2);
	client2->async_connect (boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::loopback (), listen_endpoint2.port () }, connect_handler);

	ASSERT_TIMELY (3s, 2 == connection_attempts && server1_sockets != nullptr && server2_sockets != nullptr);
	auto dummy_channel1 = std::make_shared<nano::transport::channel_tcp> (node1, client1);
	auto dummy_channel2 = std::make_shared<nano::transport::channel_tcp> (node2, client2);
	ASSERT_NE (nano::transport::map_endpoint_to_v6 (dummy_channel1->get_endpoint ()), nano::transport::map_endpoint_to_v6 (dummy_channel2->get_endpoint ()));
	// Use the aggregator from node1 only, making requests from both nodes
	node1.aggregator.add (dummy_channel1, request);
	node1.aggregator.add (dummy_channel2, request);
	ASSERT_EQ (2, node1.aggregator.size ());
	// For the first request it generates the vote, for the second it uses the generated vote
	ASSERT_TIMELY (3s, node1.aggregator.empty ());
	ASSERT_EQ (2, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 0 == node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes) + node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_late_hashes));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes) + node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_late_votes));
	ASSERT_TIMELY (3s, 0 == node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
}

TEST (request_aggregator, split)
{
	constexpr size_t max_vbh = nano::network::confirm_ack_hashes_max;
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	std::vector<std::shared_ptr<nano::block>> blocks;
	auto previous = nano::dev::genesis->hash ();
	// Add max_vbh + 1 blocks and request votes for them
	for (size_t i (0); i <= max_vbh; ++i)
	{
		nano::block_builder builder;
		blocks.push_back (builder
						  .state ()
						  .account (nano::dev::genesis_key.pub)
						  .previous (previous)
						  .representative (nano::dev::genesis_key.pub)
						  .balance (nano::dev::constants.genesis_amount - (i + 1))
						  .link (nano::dev::genesis_key.pub)
						  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						  .work (*system.work.generate (previous))
						  .build ());
		auto const & block = blocks.back ();
		previous = block->hash ();
		ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *block).code);
		request.emplace_back (block->hash (), block->root ());
	}
	// Confirm all blocks
	node.block_confirm (blocks.back ());
	auto election (node.active.election (blocks.back ()->qualified_root ()));
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (5s, max_vbh + 2 == node.ledger.cache.cemented_count);
	ASSERT_EQ (max_vbh + 1, request.size ());
	auto client = std::make_shared<nano::client_socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client);
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// Two votes were sent, the first one for 12 hashes and the second one for 1 hash
	ASSERT_EQ (1, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 13 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

TEST (request_aggregator, channel_lifetime)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	{
		// The aggregator should extend the lifetime of the channel
		auto client = std::make_shared<nano::client_socket> (node);
		std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client);
		node.aggregator.add (dummy_channel, request);
	}
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
}

TEST (request_aggregator, channel_update)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	std::weak_ptr<nano::transport::channel> channel1_w;
	{
		auto client1 = std::make_shared<nano::client_socket> (node);
		std::shared_ptr<nano::transport::channel> dummy_channel1 = std::make_shared<nano::transport::channel_tcp> (node, client1);
		channel1_w = dummy_channel1;
		node.aggregator.add (dummy_channel1, request);
		auto client2 = std::make_shared<nano::client_socket> (node);
		std::shared_ptr<nano::transport::channel> dummy_channel2 = std::make_shared<nano::transport::channel_tcp> (node, client2);
		// The aggregator then hold channel2 and drop channel1
		node.aggregator.add (dummy_channel2, request);
	}
	// Both requests were for the same endpoint, so only one pool should exist
	ASSERT_EQ (1, node.aggregator.size ());
	// channel1 is not being held anymore
	ASSERT_EQ (nullptr, channel1_w.lock ());
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes) == 0);
}

TEST (request_aggregator, channel_max_queue)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	node_config.max_queued_requests = 1;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto client = std::make_shared<nano::client_socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client);
	node.aggregator.add (dummy_channel, request);
	node.aggregator.add (dummy_channel, request);
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
}

TEST (request_aggregator, unique)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto client = std::make_shared<nano::client_socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client);
	node.aggregator.add (dummy_channel, request);
	node.aggregator.add (dummy_channel, request);
	node.aggregator.add (dummy_channel, request);
	node.aggregator.add (dummy_channel, request);
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
}

namespace nano
{
TEST (request_aggregator, cannot_vote)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_request_loop = true;
	auto & node (*system.add_node (flags));
	// This prevents activation of blocks which are cemented
	node.confirmation_height_processor.cemented_observers.clear ();
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance ().number () - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.process (*send1).code);
	ASSERT_EQ (nano::process_result::progress, node.process (*send2).code);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_FALSE (node.ledger.dependents_confirmed (node.store.tx_begin_read (), *send2));

	std::vector<std::pair<nano::block_hash, nano::root>> request;
	// Correct hash, correct root
	request.emplace_back (send2->hash (), send2->root ());
	// Incorrect hash, correct root
	request.emplace_back (1, send2->root ());
	auto client = std::make_shared<nano::client_socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client);
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 2 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));

	// With an ongoing election
	node.block_confirm (send2);
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 4 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));

	// Confirm send1
	node.block_confirm (send1);
	auto election (node.active.election (send1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (3s, node.ledger.dependents_confirmed (node.store.tx_begin_read (), *send2));
	node.aggregator.add (dummy_channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (3, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_EQ (4, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 <= node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}
}
