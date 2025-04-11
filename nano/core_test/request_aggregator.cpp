#include <nano/lib/blocks.hpp>
#include <nano/lib/function.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/local_vote_history.hpp>
#include <nano/node/request_aggregator.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/secure/vote.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>

using namespace std::chrono_literals;

TEST (request_aggregator, one)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	nano::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node (*system.add_node (node_config, node_flags));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();

	std::vector<std::pair<nano::block_hash, nano::root>> request{ { send1->hash (), send1->root () } };

	auto dummy_channel = nano::test::fake_channel (node);

	// Not yet in the ledger
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));

	// Process and confirm
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	nano::test::confirm (node.ledger, send1);

	// In the ledger but no vote generated yet
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));

	// Already cached
	// TODO: This is outdated, aggregator should not be using cache
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY_EQ (3s, 3, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

TEST (request_aggregator, one_update)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	nano::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node (*system.add_node (node_config, node_flags));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key1;
	auto send1 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	nano::test::confirm (node.ledger, send1);
	auto send2 = nano::state_block_builder ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send2));
	nano::test::confirm (node.ledger, send2);
	auto receive1 = nano::state_block_builder ()
					.account (key1.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::Knano_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), receive1));
	nano::test::confirm (node.ledger, receive1);

	auto dummy_channel = nano::test::fake_channel (node);

	std::vector<std::pair<nano::block_hash, nano::root>> request1{ { send2->hash (), send2->root () } };
	node.aggregator.request (request1, dummy_channel);

	// Update the pool of requests with another hash
	std::vector<std::pair<nano::block_hash, nano::root>> request2{ { receive1->hash (), receive1->root () } };
	node.aggregator.request (request2, dummy_channel);

	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes))
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));

	ASSERT_TIMELY (3s, 0 < node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_hashes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
}

TEST (request_aggregator, two)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	nano::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node (*system.add_node (node_config, node_flags));
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
	nano::test::confirm (node.ledger, send1);
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
	nano::test::confirm (node.ledger, send2);
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), receive1));
	nano::test::confirm (node.ledger, receive1);

	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send2->hash (), send2->root ());
	request.emplace_back (receive1->hash (), receive1->root ());

	auto dummy_channel = nano::test::fake_channel (node);

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
	ASSERT_TIMELY_EQ (3s, 4, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
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
	node_config.backlog_scan.enable = false;
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
	ASSERT_EQ (nano::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	nano::test::confirm (node1.ledger, send1);

	auto dummy_channel1 = std::make_shared<nano::transport::inproc::channel> (node1, node1);
	auto dummy_channel2 = std::make_shared<nano::transport::inproc::channel> (node2, node2);
	ASSERT_NE (nano::transport::map_endpoint_to_v6 (dummy_channel1->get_remote_endpoint ()), nano::transport::map_endpoint_to_v6 (dummy_channel2->get_remote_endpoint ()));

	std::vector<std::pair<nano::block_hash, nano::root>> request{ { send1->hash (), send1->root () } };

	// For the first request, aggregator should generate a new vote
	node1.aggregator.request (request, dummy_channel1);
	ASSERT_TIMELY (5s, node1.aggregator.empty ());

	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));

	ASSERT_TIMELY_EQ (5s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));

	// For the second request, aggregator should use the cache
	// TODO: This is outdated, aggregator should not be using cache
	node1.aggregator.request (request, dummy_channel1);
	ASSERT_TIMELY (5s, node1.aggregator.empty ());

	ASSERT_TIMELY_EQ (5s, 2, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));

	ASSERT_TIMELY_EQ (5s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY_EQ (5s, 2, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (5s, 2, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TIMELY_EQ (3s, 0, node1.stats.count (nano::stat::type::requests, nano::stat::detail::requests_cannot_vote));
}

TEST (request_aggregator, split)
{
	size_t max_vbh = nano::network::confirm_ack_hashes_max;
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	nano::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node (*system.add_node (node_config, node_flags));
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
	{
		// Confirm all blocks
		auto tx = node.ledger.tx_begin_write ();
		node.ledger.confirm (tx, blocks.back ()->hash ());
	}
	ASSERT_TIMELY_EQ (5s, max_vbh + 2, node.ledger.cemented_count ());
	ASSERT_EQ (max_vbh + 1, request.size ());

	auto dummy_channel = nano::test::fake_channel (node);

	node.aggregator.request (request, dummy_channel);
	// In the ledger but no vote generated yet
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// Two votes were sent, the first one for 12 hashes and the second one for 1 hash
	ASSERT_EQ (1, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, nano::network::confirm_ack_hashes_max + 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
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
	node_config.backlog_scan.enable = false;
	node_config.request_aggregator.max_queue = 0;
	nano::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node (*system.add_node (node_config, node_flags));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());

	auto dummy_channel = nano::test::fake_channel (node);

	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	ASSERT_LT (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
}

// TODO: Deduplication is a concern for the requesting node, not the aggregator which should be stateless and fairly service all peers
TEST (request_aggregator, DISABLED_unique)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	std::vector<std::pair<nano::block_hash, nano::root>> request;
	request.emplace_back (send1->hash (), send1->root ());

	auto dummy_channel = nano::test::fake_channel (node);

	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
}

TEST (request_aggregator, cannot_vote)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_request_loop = true;
	flags.disable_rep_crawler = true;
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

	auto dummy_channel = nano::test::fake_channel (node);

	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 2, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_non_final));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));

	// With an ongoing election
	node.start_election (send2);
	ASSERT_TIMELY (5s, node.active.election (send2->qualified_root ()));

	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_TIMELY_EQ (3s, 4, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_non_final));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));

	// Confirm send1 and send2
	nano::test::confirm (node.ledger, { send1, send2 });

	node.aggregator.request (request, dummy_channel);
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (3, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::aggregator, nano::stat::detail::aggregator_dropped));
	ASSERT_EQ (4, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_non_final));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY_EQ (3s, 1, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::requests, nano::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 <= node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::out));
}

namespace
{
std::future<nano::confirm_ack> observe_confirm_ack (std::shared_ptr<nano::transport::test_channel> const & channel)
{
	std::promise<nano::confirm_ack> promise;
	auto future = promise.get_future ();

	struct confirm_ack_visitor : public nano::message_visitor
	{
		std::optional<nano::confirm_ack> result;

		void confirm_ack (nano::confirm_ack const & msg) override
		{
			result = msg;
		}
	};

	channel->observers.clear ();
	channel->observers.add (nano::wrap_move_only ([&, promise = std::move (promise)] (nano::message const & message, nano::transport::traffic_type const & type) mutable {
		confirm_ack_visitor visitor{};
		message.visit (visitor);
		if (visitor.result)
		{
			promise.set_value (visitor.result.value ());
		}
	}));

	return future;
}
}

/*
 * Request for a forked open block should return vote for the correct fork alternative
 */
TEST (request_aggregator, forked_open)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// Voting needs a rep key set up on the node
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Setup two forks of the open block
	nano::keypair key;
	nano::block_builder builder;
	auto send0 = builder.send ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 500)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open0 = builder.open ()
				 .source (send0->hash ())
				 .representative (1)
				 .account (key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (key.pub))
				 .build ();
	auto open1 = builder.open ()
				 .source (send0->hash ())
				 .representative (2)
				 .account (key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (key.pub))
				 .build ();

	nano::test::process (node, { send0, open0 });
	nano::test::confirm (node, { open0 });

	auto channel = nano::test::test_channel (node);
	auto future = observe_confirm_ack (channel);

	// Request vote for the wrong fork
	std::vector<std::pair<nano::block_hash, nano::root>> request{ { open1->hash (), open1->root () } };
	ASSERT_TRUE (node.aggregator.request (request, channel));

	ASSERT_EQ (future.wait_for (5s), std::future_status::ready);

	auto ack = future.get ();
	ASSERT_EQ (ack.vote->hashes.size (), 1);
	ASSERT_EQ (ack.vote->hashes[0], open0->hash ()); // Vote for the correct fork alternative
	ASSERT_EQ (ack.vote->account, nano::dev::genesis_key.pub);
}

/*
 * Request for a conflicting epoch block should return vote for the correct alternative
 */
TEST (request_aggregator, epoch_conflict)
{
	nano::test::system system;

	nano::node_flags node_flags;
	// Workaround for vote spacing dropping requests with the same root
	// FIXME: Vote spacing should use full qualified root
	node_flags.disable_rep_crawler = true;
	auto & node = *system.add_node (node_flags);

	// Voting needs a rep key set up on the node
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Setup the initial chain and the conflicting blocks
	nano::keypair key;
	nano::keypair epoch_signer (nano::dev::genesis_key);
	nano::state_block_builder builder;

	// Create initial chain: send -> open -> change
	auto send = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();

	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (1)
				.link (send->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();

	// Change block root is the open block hash, qualified root: {open, open}
	auto change = builder.make_block ()
				  .account (key.pub)
				  .previous (open->hash ())
				  .representative (key.pub)
				  .balance (1)
				  .link (0)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (open->hash ()))
				  .build ();

	// Pending entry is needed first to process the epoch open block
	auto pending = builder.make_block ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (send->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 2)
				   .link (change->root ().as_account ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (*system.work.generate (send->hash ()))
				   .build ();

	// Create conflicting epoch block with the same root as the change block, qualified root: {open, 0}
	// This block is intentionally not processed immediately so the node doesn't know about it
	auto epoch_open = builder.make_block ()
					  .account (change->root ().as_account ())
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (node.ledger.epoch_link (nano::epoch::epoch_1))
					  .sign (epoch_signer.prv, epoch_signer.pub)
					  .work (*system.work.generate (open->hash ()))
					  .build ();

	// Process and confirm the initial chain with the change block
	nano::test::process (node, { send, open, change });
	nano::test::confirm (node, { change });
	ASSERT_TIMELY (5s, node.block_confirmed (change->hash ()));

	auto channel = nano::test::test_channel (node);

	// Request vote for the conflicting epoch block
	std::vector<std::pair<nano::block_hash, nano::root>> request{ { epoch_open->hash (), epoch_open->root () } };
	auto future1 = observe_confirm_ack (channel);
	ASSERT_TRUE (node.aggregator.request (request, channel));

	ASSERT_EQ (future1.wait_for (5s), std::future_status::ready);

	auto ack1 = future1.get ();
	ASSERT_EQ (ack1.vote->hashes.size (), 1);
	ASSERT_EQ (ack1.vote->hashes[0], change->hash ()); // Vote for the correct alternative (change block)
	ASSERT_EQ (ack1.vote->account, nano::dev::genesis_key.pub);

	// Process the conflicting epoch block
	nano::test::process (node, { pending, epoch_open });
	nano::test::confirm (node, { pending, epoch_open });

	// Workaround for vote spacing dropping requests with the same root
	// FIXME: Vote spacing should use full qualified root
	WAIT (1s);

	// Request vote for the conflicting epoch block again
	auto future2 = observe_confirm_ack (channel);
	ASSERT_TRUE (node.aggregator.request (request, channel));

	ASSERT_EQ (future2.wait_for (5s), std::future_status::ready);

	auto ack2 = future2.get ();
	ASSERT_EQ (ack2.vote->hashes.size (), 1);
	ASSERT_EQ (ack2.vote->hashes[0], epoch_open->hash ()); // Vote for the epoch block
	ASSERT_EQ (ack2.vote->account, nano::dev::genesis_key.pub);
}

/*
 * Request for multiple cemented blocks in a chain should generate votes regardless of vote spacing
 */
TEST (request_aggregator, cemented_no_spacing)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// Voting needs a rep key set up on the node
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Create a chain of 3 blocks: send1 -> send2 -> send3
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
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();

	auto send3 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 3)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();

	// Process and confirm all blocks in the chain
	nano::test::process (node, { send1, send2, send3 });
	nano::test::confirm (node, { send1, send2, send3 });
	ASSERT_TRUE (node.block_confirmed (send3->hash ()));

	auto channel = nano::test::test_channel (node);

	// Request votes for blocks at different positions in the chain
	std::vector<std::pair<nano::block_hash, nano::root>> request{
		{ send1->hash (), send1->root () },
		{ send2->hash (), send2->root () },
		{ send3->hash (), send3->root () }
	};

	// Request votes for all blocks
	auto future = observe_confirm_ack (channel);
	ASSERT_TRUE (node.aggregator.request (request, channel));

	// Wait for the votes
	ASSERT_EQ (future.wait_for (5s), std::future_status::ready);
	auto ack = future.get ();

	// Verify we got votes for all blocks in the chain
	ASSERT_EQ (ack.vote->hashes.size (), 3);
	ASSERT_EQ (ack.vote->account, nano::dev::genesis_key.pub);

	// Verify individual vote properties
	std::set<nano::block_hash> voted_hashes;
	for (auto const & hash : ack.vote->hashes)
	{
		voted_hashes.insert (hash);
	}

	// Verify we got votes for all three blocks
	ASSERT_TRUE (voted_hashes.find (send1->hash ()) != voted_hashes.end ());
	ASSERT_TRUE (voted_hashes.find (send2->hash ()) != voted_hashes.end ());
	ASSERT_TRUE (voted_hashes.find (send3->hash ()) != voted_hashes.end ());
}