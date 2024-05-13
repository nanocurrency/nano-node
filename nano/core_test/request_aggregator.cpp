#include <nano/lib/blocks.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/local_vote_history.hpp>
#include <nano/node/request_aggregator.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (request_aggregator, one)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
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
				 .build ();
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto client = std::make_shared<nano::transport::socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client, std::weak_ptr<nano::transport::tcp_server>{});
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	// Not yet in the ledger
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	node.aggregator.request (request, dummy_channel);
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	node.aggregator.request (request, dummy_channel);
	// Already cached
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY_EQ (3s, 3, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

TEST (request_aggregator, one_update)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
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
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	node.confirming_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node.ledger.confirmed.block_exists_or_pruned (node.ledger.tx_begin_read (), send1->hash ()));
	auto send2 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send2));
	auto receive1 = nano::state_block_builder ()
					.account (key1.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::Gxrb_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), receive1));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send2->hash (), send2->root ());
	auto client = std::make_shared<nano::transport::socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client, std::weak_ptr<nano::transport::tcp_server>{});
	node.aggregator.request (request, dummy_channel);
	request.clear ();
	request.emplace_back (receive1->hash (), receive1->root ());
	// Update the pool of requests with another hash
	node.aggregator.request (request, dummy_channel);
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes))
	ASSERT_TRUE (node.aggregator.empty ());
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	size_t count = 0;
	ASSERT_TIMELY_EQ (3s, 1, (count = node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes)));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

TEST (request_aggregator, two)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
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
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	node.confirming_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node.ledger.confirmed.block_exists_or_pruned (node.ledger.tx_begin_read (), send1->hash ()));
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	auto receive1 = builder.make_block ()
					.account (key1.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.balance (1)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send2));
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), receive1));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send2->hash (), send2->root ());
	request.emplace_back (receive1->hash (), receive1->root ());
	auto client = std::make_shared<nano::transport::socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client, std::weak_ptr<nano::transport::tcp_server>{});
	// Process both blocks
	node.aggregator.request (request, dummy_channel);
	// One vote should be generated for both blocks
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// The same request should now send the cached vote
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
	// Make sure the cached vote is for both hashes
	auto vote1 (node.history.votes (send2->root (), send2->hash ()));
	auto vote2 (node.history.votes (receive1->root (), receive1->hash ()));
	ASSERT_EQ (1, vote1.size ());
	ASSERT_EQ (1, vote2.size ());
	ASSERT_EQ (vote1.front (), vote2.front ());
}

TEST (request_aggregator, two_endpoints)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	nano::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node1 (*system.add_node (node_config, node_flags));
	node_config.peering_port = system.get_available_port ();
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
				 .build ();
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	ASSERT_EQ (nano::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	auto dummy_channel1 = std::make_shared<nano::transport::inproc::channel> (node1, node1);
	auto dummy_channel2 = std::make_shared<nano::transport::inproc::channel> (node2, node2);
	ASSERT_NE (nano::transport::map_endpoint_to_v6 (dummy_channel1->get_endpoint ()), nano::transport::map_endpoint_to_v6 (dummy_channel2->get_endpoint ()));

	// For the first request, aggregator should generate a new vote
	node1.aggregator.request (request, dummy_channel1);
	ASSERT_TIMELY (5s, node1.aggregator.empty ());

	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));

	ASSERT_TIMELY_EQ (5s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY_EQ (3s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_TIMELY_EQ (3s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));

	// For the second request, aggregator should use the cache
	node1.aggregator.request (request, dummy_channel1);
	ASSERT_TIMELY (5s, node1.aggregator.empty ());

	ASSERT_TIMELY_EQ (5s, 2, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));

	ASSERT_TIMELY_EQ (5s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY_EQ (3s, 1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_TIMELY_EQ (3s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
}

TEST (request_aggregator, split)
{
	constexpr size_t max_vbh = nano::network::confirm_ack_hashes_max;
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
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
		ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), block));
		request.emplace_back (block->hash (), block->root ());
	}
	// Confirm all blocks
	node.ledger.confirm (node.ledger.tx_begin_write (), blocks.back ()->hash ());
	ASSERT_TIMELY_EQ (5s, max_vbh + 2, node.ledger.cemented_count ());
	ASSERT_EQ (max_vbh + 1, request.size ());
	auto client = std::make_shared<nano::transport::socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client, std::weak_ptr<nano::transport::tcp_server>{});
	node.aggregator.request (request, dummy_channel);
	// In the ledger but no vote generated yet
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// Two votes were sent, the first one for 12 hashes and the second one for 1 hash
	ASSERT_EQ (1, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 13, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

TEST (request_aggregator, channel_max_queue)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	node_config.request_aggregator.max_queue = 0;
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
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto client = std::make_shared<nano::transport::socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client, std::weak_ptr<nano::transport::tcp_server>{});
	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	ASSERT_LT (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
}

// TODO: Deduplication is a concern for the requesting node, not the aggregator which should be stateless and fairly service all peers
TEST (request_aggregator, DISABLED_unique)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
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
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto client = std::make_shared<nano::transport::socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client, std::weak_ptr<nano::transport::tcp_server>{});
	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
}

namespace nano
{
TEST (request_aggregator, cannot_vote)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_request_loop = true;
	auto & node (*system.add_node (flags));
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance_field ().value ().number () - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send1));
	ASSERT_EQ (nano::block_status::progress, node.process (send2));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_FALSE (node.ledger.dependents_confirmed (node.ledger.tx_begin_read (), *send2));

	std::vector<std::pair<nano::block_hash, nano::root>> request;
	// Correct hash, correct root
	request.emplace_back (send2->hash (), send2->root ());
	// Incorrect hash, correct root
	request.emplace_back (1, send2->root ());
	auto client = std::make_shared<nano::transport::socket> (node);
	std::shared_ptr<nano::transport::channel> dummy_channel = std::make_shared<nano::transport::channel_tcp> (node, client, std::weak_ptr<nano::transport::tcp_server>{});
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));

	// With an ongoing election
	node.start_election (send2);
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 4, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));

	// Confirm send1
	node.start_election (send1);
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send1->qualified_root ()));
	election->force_confirm ();
	ASSERT_TIMELY (3s, node.ledger.dependents_confirmed (node.ledger.tx_begin_read (), *send2));
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (3, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_EQ (4, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 <= node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}
}
