#include <celerix/lib/blocks.hpp>
#include <celerix/lib/jsonconfig.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/confirming_set.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/local_vote_history.hpp>
#include <celerix/node/request_aggregator.hpp>
#include <celerix/node/transport/fake.hpp>
#include <celerix/node/transport/inproc.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>
#include <celerix/test_common/network.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (request_aggregator, one)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();

	std::vector<std::pair<celerix::block_hash, celerix::root>> request{ { send1->hash (), send1->root () } };

	auto dummy_channel = celerix::test::fake_channel (node);

	// Not yet in the ledger
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));

	// Process and confirm
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	celerix::test::confirm (node.ledger, send1);

	// In the ledger but no vote generated yet
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY (3s, 0 < node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));

	// Already cached
	// TODO: This is outdated, aggregator should not be using cache
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY_EQ (3s, 3, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_accepted));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
}

TEST (request_aggregator, one_update)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key1;
	auto send1 = celerix::state_block_builder ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	celerix::test::confirm (node.ledger, send1);
	auto send2 = celerix::state_block_builder ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send2));
	celerix::test::confirm (node.ledger, send2);
	auto receive1 = celerix::state_block_builder ()
					.account (key1.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), receive1));
	celerix::test::confirm (node.ledger, receive1);

	auto dummy_channel = celerix::test::fake_channel (node);

	std::vector<std::pair<celerix::block_hash, celerix::root>> request1{ { send2->hash (), send2->root () } };
	node.aggregator.request (request1, dummy_channel);

	// Update the pool of requests with another hash
	std::vector<std::pair<celerix::block_hash, celerix::root>> request2{ { receive1->hash (), receive1->root () } };
	node.aggregator.request (request2, dummy_channel);

	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes))
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_accepted));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_hashes));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));

	ASSERT_TIMELY (3s, 0 < node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_cached_hashes));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_cannot_vote));
}

TEST (request_aggregator, two)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key1;
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	celerix::test::confirm (node.ledger, send1);
	auto send2 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	auto receive1 = builder.make_block ()
					.account (key1.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (1)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send2));
	celerix::test::confirm (node.ledger, send2);
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), receive1));
	celerix::test::confirm (node.ledger, receive1);

	std::vector<std::pair<celerix::block_hash, celerix::root>> request;
	request.emplace_back (send2->hash (), send2->root ());
	request.emplace_back (receive1->hash (), receive1->root ());

	auto dummy_channel = celerix::test::fake_channel (node);

	// Process both blocks
	node.aggregator.request (request, dummy_channel);
	// One vote should be generated for both blocks
	ASSERT_TIMELY (3s, 0 < node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// The same request should now send the cached vote
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (3s, 4, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
	// Make sure the cached vote is for both hashes
	auto vote1 (node.history.votes (send2->root (), send2->hash ()));
	auto vote2 (node.history.votes (receive1->root (), receive1->hash ()));
	ASSERT_EQ (1, vote1.size ());
	ASSERT_EQ (1, vote2.size ());
	ASSERT_EQ (vote1.front (), vote2.front ());
}

TEST (request_aggregator, two_endpoints)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	celerix::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node1 (*system.add_node (node_config, node_flags));
	node_config.peering_port = system.get_available_port ();
	auto & node2 (*system.add_node (node_config, node_flags));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	celerix::test::confirm (node1.ledger, send1);

	auto dummy_channel1 = std::make_shared<celerix::transport::inproc::channel> (node1, node1);
	auto dummy_channel2 = std::make_shared<celerix::transport::inproc::channel> (node2, node2);
	ASSERT_NE (celerix::transport::map_endpoint_to_v6 (dummy_channel1->get_remote_endpoint ()), celerix::transport::map_endpoint_to_v6 (dummy_channel2->get_remote_endpoint ()));

	std::vector<std::pair<celerix::block_hash, celerix::root>> request{ { send1->hash (), send1->root () } };

	// For the first request, aggregator should generate a new vote
	node1.aggregator.request (request, dummy_channel1);
	ASSERT_TIMELY (5s, node1.aggregator.empty ());

	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));

	ASSERT_TIMELY_EQ (5s, 0, node1.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node1.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_cannot_vote));

	// For the second request, aggregator should use the cache
	// TODO: This is outdated, aggregator should not be using cache
	node1.aggregator.request (request, dummy_channel1);
	ASSERT_TIMELY (5s, node1.aggregator.empty ());

	ASSERT_TIMELY_EQ (5s, 2, node1.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node1.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));

	ASSERT_TIMELY_EQ (5s, 0, node1.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (5s, 2, node1.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (5s, 2, node1.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node1.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_cannot_vote));
}

TEST (request_aggregator, split)
{
	size_t max_vbh = celerix::network::confirm_ack_hashes_max;
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	std::vector<std::pair<celerix::block_hash, celerix::root>> request;
	std::vector<std::shared_ptr<celerix::block>> blocks;
	auto previous = celerix::dev::genesis->hash ();
	// Add max_vbh + 1 blocks and request votes for them
	for (size_t i (0); i <= max_vbh; ++i)
	{
		celerix::block_builder builder;
		blocks.push_back (builder
						  .state ()
						  .account (celerix::dev::genesis_key.pub)
						  .previous (previous)
						  .representative (celerix::dev::genesis_key.pub)
						  .balance (celerix::dev::constants.genesis_amount - (i + 1))
						  .link (celerix::dev::genesis_key.pub)
						  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
						  .work (*system.work.generate (previous))
						  .build ());
		auto const & block = blocks.back ();
		previous = block->hash ();
		ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), block));
		request.emplace_back (block->hash (), block->root ());
	}
	{
		// Confirm all blocks
		auto tx = node.ledger.tx_begin_write ();
		node.ledger.confirm (tx, blocks.back ()->hash ());
	}
	ASSERT_TIMELY_EQ (5s, max_vbh + 2, node.ledger.cemented_count ());
	ASSERT_EQ (max_vbh + 1, request.size ());

	auto dummy_channel = celerix::test::fake_channel (node);

	node.aggregator.request (request, dummy_channel);
	// In the ledger but no vote generated yet
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// Two votes were sent, the first one for 12 hashes and the second one for 1 hash
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, celerix::network::confirm_ack_hashes_max + 1, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
}

TEST (request_aggregator, channel_max_queue)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	node_config.request_aggregator.max_queue = 0;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	std::vector<std::pair<celerix::block_hash, celerix::root>> request;
	request.emplace_back (send1->hash (), send1->root ());

	auto dummy_channel = celerix::test::fake_channel (node);

	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	ASSERT_LT (0, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));
}

// TODO: Deduplication is a concern for the requesting node, not the aggregator which should be stateless and fairly service all peers
TEST (request_aggregator, DISABLED_unique)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	std::vector<std::pair<celerix::block_hash, celerix::root>> request;
	request.emplace_back (send1->hash (), send1->root ());

	auto dummy_channel = celerix::test::fake_channel (node);

	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
}

TEST (request_aggregator, cannot_vote)
{
	celerix::test::system system;
	celerix::node_flags flags;
	flags.disable_request_loop = true;
	auto & node (*system.add_node (flags));
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance_field ().value ().number () - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, node.process (send1));
	ASSERT_EQ (celerix::block_status::progress, node.process (send2));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	ASSERT_FALSE (node.ledger.dependents_confirmed (node.ledger.tx_begin_read (), *send2));

	std::vector<std::pair<celerix::block_hash, celerix::root>> request;
	// Correct hash, correct root
	request.emplace_back (send2->hash (), send2->root ());
	// Incorrect hash, correct root
	request.emplace_back (1, send2->root ());

	auto dummy_channel = celerix::test::fake_channel (node);

	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_non_final));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));

	// With an ongoing election
	node.start_election (send2);
	ASSERT_TIMELY (5s, node.active.election (send2->qualified_root ()));

	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 4, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_non_final));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));

	// Confirm send1 and send2
	celerix::test::confirm (node.ledger, { send1, send2 });

	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (3, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::aggregator, celerix::stat::detail::aggregator_dropped));
	ASSERT_EQ (4, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_non_final));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::requests, celerix::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 <= node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
}
