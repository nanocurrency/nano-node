#include <celerix/lib/blocks.hpp>
#include <celerix/lib/jsonconfig.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/transport/fake.hpp>
#include <celerix/node/transport/inproc.hpp>
#include <celerix/node/vote_processor.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/vote.hpp>
#include <celerix/test_common/chains.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (vote_processor, codes)
{
	celerix::test::system system;
	auto node_config = system.default_config ();
	// Disable all election schedulers
	node_config.backlog_scan.enable = false;
	node_config.hinted_scheduler.enable = false;
	node_config.optimistic_scheduler.enable = false;
	auto & node = *system.add_node (node_config);

	auto blocks = celerix::test::setup_chain (system, node, 1, celerix::dev::genesis_key, false);
	auto vote = celerix::test::make_vote (celerix::dev::genesis_key, { blocks[0] }, celerix::vote::timestamp_min * 1, 0);
	auto vote_invalid = std::make_shared<celerix::vote> (*vote);
	vote_invalid->signature.bytes[0] ^= 1;
	auto channel (std::make_shared<celerix::transport::inproc::channel> (node, node));

	// Invalid signature
	ASSERT_EQ (celerix::vote_code::invalid, node.vote_processor.vote_blocking (vote_invalid, channel));

	// No ongoing election (vote goes to vote cache)
	ASSERT_EQ (celerix::vote_code::indeterminate, node.vote_processor.vote_blocking (vote, channel));

	// Clear vote cache before starting election
	node.vote_cache.clear ();

	// First vote from an account for an ongoing election
	node.start_election (blocks[0]);
	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (blocks[0]->qualified_root ()));
	ASSERT_EQ (celerix::vote_code::vote, node.vote_processor.vote_blocking (vote, channel));

	// Processing the same vote is a replay
	ASSERT_EQ (celerix::vote_code::replay, node.vote_processor.vote_blocking (vote, channel));

	// Invalid takes precedence
	ASSERT_EQ (celerix::vote_code::invalid, node.vote_processor.vote_blocking (vote_invalid, channel));

	// Once the election is removed (confirmed / dropped) the vote is again indeterminate
	ASSERT_TRUE (node.active.erase (blocks[0]->qualified_root ()));
	ASSERT_EQ (celerix::vote_code::indeterminate, node.vote_processor.vote_blocking (vote, channel));
}

TEST (vote_processor, invalid_signature)
{
	celerix::test::system system{ 1 };
	auto & node = *system.nodes[0];
	auto chain = celerix::test::setup_chain (system, node, 1, celerix::dev::genesis_key, false);
	celerix::keypair key;
	auto vote = std::make_shared<celerix::vote> (key.pub, key.prv, celerix::vote::timestamp_min * 1, 0, std::vector<celerix::block_hash>{ chain[0]->hash () });
	auto vote_invalid = std::make_shared<celerix::vote> (*vote);
	vote_invalid->signature.bytes[0] ^= 1;
	auto channel = std::make_shared<celerix::transport::inproc::channel> (node, node);

	auto election = celerix::test::start_election (system, node, chain[0]->hash ());
	ASSERT_NE (election, nullptr);
	ASSERT_EQ (1, election->votes ().size ());

	node.vote_processor.vote (vote_invalid, channel);
	ASSERT_TIMELY_EQ (5s, 1, election->votes ().size ());
	node.vote_processor.vote (vote, channel);
	ASSERT_TIMELY_EQ (5s, 2, election->votes ().size ());
}

TEST (vote_processor, overflow)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	node_flags.vote_processor_capacity = 1;
	auto & node (*system.add_node (node_flags));
	celerix::keypair key;
	auto vote = celerix::test::make_vote (key, { celerix::dev::genesis }, celerix::vote::timestamp_min * 1, 0);
	auto channel (std::make_shared<celerix::transport::inproc::channel> (node, node));
	auto start_time = std::chrono::system_clock::now ();

	// No way to lock the processor, but queueing votes in quick succession must result in overflow
	size_t not_processed{ 0 };
	size_t const total{ 1000 };
	for (unsigned i = 0; i < total; ++i)
	{
		if (!node.vote_processor.vote (vote, channel))
		{
			++not_processed;
		}
	}
	ASSERT_GT (not_processed, 0);
	ASSERT_LT (not_processed, total);
	ASSERT_EQ (not_processed, node.stats.count (celerix::stat::type::vote_processor, celerix::stat::detail::overfill));

	// check that it did not timeout
	ASSERT_LT (std::chrono::system_clock::now () - start_time, 10s);
}

TEST (vote_processor, weights)
{
	celerix::test::system system (4);
	auto & node (*system.nodes[0]);

	// Create representatives of different weight levels
	auto const stake = node.config.online_weight_minimum.number ();
	auto const level0 = stake / 5000; // 0.02%
	auto const level1 = stake / 500; // 0.2%
	auto const level2 = stake / 50; // 2%

	celerix::keypair key0;
	celerix::keypair key1;
	celerix::keypair key2;

	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key0.prv);
	system.wallet (2)->insert_adhoc (key1.prv);
	system.wallet (3)->insert_adhoc (key2.prv);
	system.wallet (1)->store.representative_set (system.nodes[1]->wallets.tx_begin_write (), key0.pub);
	system.wallet (2)->store.representative_set (system.nodes[2]->wallets.tx_begin_write (), key1.pub);
	system.wallet (3)->store.representative_set (system.nodes[3]->wallets.tx_begin_write (), key2.pub);
	system.wallet (0)->send_sync (celerix::dev::genesis_key.pub, key0.pub, level0);
	system.wallet (0)->send_sync (celerix::dev::genesis_key.pub, key1.pub, level1);
	system.wallet (0)->send_sync (celerix::dev::genesis_key.pub, key2.pub, level2);

	// Wait for representatives
	ASSERT_TIMELY_EQ (10s, node.ledger.cache.rep_weights.get_rep_amounts ().size (), 4);

	// Wait for rep tiers to be updated
	node.stats.clear ();
	ASSERT_TIMELY (5s, node.stats.count (celerix::stat::type::rep_tiers, celerix::stat::detail::updated) >= 2);

	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key0.pub), celerix::rep_tier::none);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key1.pub), celerix::rep_tier::tier_1);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key2.pub), celerix::rep_tier::tier_2);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (celerix::dev::genesis_key.pub), celerix::rep_tier::tier_3);
}

// Issue that tracks last changes on this test: https://github.com/celerixcurrency/celerix-node/issues/3485
// Reopen in case the nondeterministic failure appears again.
// Checks local votes (a vote with a key that is in the node's wallet) are not re-broadcast when received.
// Nodes should not relay their own votes
TEST (vote_processor, no_broadcast_local)
{
	celerix::test::system system;
	celerix::node_flags flags;
	flags.disable_request_loop = true;
	celerix::node_config config1, config2;
	config1.representative_vote_weight_minimum = 0;
	config1.backlog_scan.enable = false;
	auto & node (*system.add_node (config1, flags));
	config2.representative_vote_weight_minimum = 0;
	config2.backlog_scan.enable = false;
	config2.peering_port = system.get_available_port ();
	system.add_node (config2, flags);
	celerix::block_builder builder;
	std::error_code ec;
	// Reduce the weight of genesis to 2x default min voting weight
	celerix::keypair key;
	std::shared_ptr<celerix::block> send = builder.state ()
										.account (celerix::dev::genesis_key.pub)
										.representative (celerix::dev::genesis_key.pub)
										.previous (celerix::dev::genesis->hash ())
										.balance (2 * node.config.vote_minimum.number ())
										.link (key.pub)
										.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
										.work (*system.work.generate (celerix::dev::genesis->hash ()))
										.build (ec);
	ASSERT_FALSE (ec);
	ASSERT_EQ (celerix::block_status::progress, node.process_local (send).value ());
	ASSERT_TIMELY (10s, !node.active.empty ());
	ASSERT_EQ (2 * node.config.vote_minimum.number (), node.weight (celerix::dev::genesis_key.pub));
	// Insert account in wallet. Votes on node are not enabled.
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	// Ensure that the node knows the genesis key in its wallet.
	node.wallets.compute_reps ();
	ASSERT_TRUE (node.wallets.reps ().exists (celerix::dev::genesis_key.pub));
	ASSERT_FALSE (node.wallets.reps ().have_half_rep ()); // Genesis balance remaining after `send' is less than the half_rep threshold
	// Process a vote with a key that is in the local wallet.
	auto vote = std::make_shared<celerix::vote> (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.prv, celerix::milliseconds_since_epoch (), celerix::vote::duration_max, std::vector<celerix::block_hash>{ send->hash () });
	ASSERT_EQ (celerix::vote_code::vote, node.vote_router.vote (vote).at (send->hash ()));
	// Make sure the vote was processed.
	auto election (node.active.election (send->qualified_root ()));
	ASSERT_NE (nullptr, election);
	auto votes (election->votes ());
	auto existing (votes.find (celerix::dev::genesis_key.pub));
	ASSERT_NE (votes.end (), existing);
	ASSERT_EQ (vote->timestamp (), existing->second.timestamp);
	// Ensure the vote, from a local representative, was not broadcast on processing - it should be flooded on vote generation instead.
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::out));
}

// Issue that tracks last changes on this test: https://github.com/celerixcurrency/celerix-node/issues/3485
// Reopen in case the nondeterministic failure appears again.
// Checks non-local votes (a vote with a key that is not in the node's wallet) are re-broadcast when received.
// Done without a representative.
TEST (vote_processor, local_broadcast_without_a_representative)
{
	celerix::test::system system;
	celerix::node_flags flags;
	flags.disable_request_loop = true;
	celerix::node_config config1, config2;
	config1.representative_vote_weight_minimum = 0;
	config1.backlog_scan.enable = false;
	auto & node (*system.add_node (config1, flags));
	config2.representative_vote_weight_minimum = 0;
	config2.backlog_scan.enable = false;
	config2.peering_port = system.get_available_port ();
	system.add_node (config2, flags);
	celerix::block_builder builder;
	std::error_code ec;
	// Reduce the weight of genesis to 2x default min voting weight
	celerix::keypair key;
	std::shared_ptr<celerix::block> send = builder.state ()
										.account (celerix::dev::genesis_key.pub)
										.representative (celerix::dev::genesis_key.pub)
										.previous (celerix::dev::genesis->hash ())
										.balance (node.config.vote_minimum.number ())
										.link (key.pub)
										.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
										.work (*system.work.generate (celerix::dev::genesis->hash ()))
										.build (ec);
	ASSERT_FALSE (ec);
	ASSERT_EQ (celerix::block_status::progress, node.process_local (send).value ());
	ASSERT_TIMELY (10s, !node.active.empty ());
	ASSERT_EQ (node.config.vote_minimum, node.weight (celerix::dev::genesis_key.pub));
	node.start_election (send);
	// Process a vote without a representative
	auto vote = std::make_shared<celerix::vote> (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.prv, celerix::milliseconds_since_epoch (), celerix::vote::duration_max, std::vector<celerix::block_hash>{ send->hash () });
	ASSERT_EQ (celerix::vote_code::vote, node.vote_router.vote (vote).at (send->hash ()));
	// Make sure the vote was processed.
	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send->qualified_root ()));
	auto votes (election->votes ());
	auto existing (votes.find (celerix::dev::genesis_key.pub));
	ASSERT_NE (votes.end (), existing);
	ASSERT_EQ (vote->timestamp (), existing->second.timestamp);
	// Ensure the vote was broadcast
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::out));
}

// Issue that tracks last changes on this test: https://github.com/celerixcurrency/celerix-node/issues/3485
// Reopen in case the nondeterministic failure appears again.
// Checks local votes (a vote with a key that is in the node's wallet) are not re-broadcast when received.
// Done with a principal representative.
TEST (vote_processor, no_broadcast_local_with_a_principal_representative)
{
	celerix::test::system system;
	celerix::node_flags flags;
	flags.disable_request_loop = true;
	celerix::node_config config1, config2;
	config1.backlog_scan.enable = false;
	auto & node (*system.add_node (config1, flags));
	config2.backlog_scan.enable = false;
	config2.peering_port = system.get_available_port ();
	system.add_node (config2, flags);
	celerix::block_builder builder;
	std::error_code ec;
	// Reduce the weight of genesis to 2x default min voting weight
	celerix::keypair key;
	std::shared_ptr<celerix::block> send = builder.state ()
										.account (celerix::dev::genesis_key.pub)
										.representative (celerix::dev::genesis_key.pub)
										.previous (celerix::dev::genesis->hash ())
										.balance (celerix::dev::constants.genesis_amount - 2 * node.config.vote_minimum.number ())
										.link (key.pub)
										.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
										.work (*system.work.generate (celerix::dev::genesis->hash ()))
										.build (ec);
	ASSERT_FALSE (ec);
	ASSERT_EQ (celerix::block_status::progress, node.process_local (send).value ());
	ASSERT_TIMELY (10s, !node.active.empty ());
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 2 * node.config.vote_minimum.number (), node.weight (celerix::dev::genesis_key.pub));
	// Insert account in wallet. Votes on node are not enabled.
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	// Ensure that the node knows the genesis key in its wallet.
	node.wallets.compute_reps ();
	ASSERT_TRUE (node.wallets.reps ().exists (celerix::dev::genesis_key.pub));
	ASSERT_TRUE (node.wallets.reps ().have_half_rep ()); // Genesis balance after `send' is over both half_rep and PR threshold.
	// Process a vote with a key that is in the local wallet.
	auto vote = std::make_shared<celerix::vote> (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.prv, celerix::milliseconds_since_epoch (), celerix::vote::duration_max, std::vector<celerix::block_hash>{ send->hash () });
	ASSERT_EQ (celerix::vote_code::vote, node.vote_router.vote (vote).at (send->hash ()));
	// Make sure the vote was processed.
	auto election (node.active.election (send->qualified_root ()));
	ASSERT_NE (nullptr, election);
	auto votes (election->votes ());
	auto existing (votes.find (celerix::dev::genesis_key.pub));
	ASSERT_NE (votes.end (), existing);
	ASSERT_EQ (vote->timestamp (), existing->second.timestamp);
	// Ensure the vote was not broadcast.
	ASSERT_EQ (0, node.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_ack, celerix::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::out));
}

/**
 * Ensure that node behaves well with votes larger than 12 hashes, which was maximum before V26
 */
TEST (vote_processor, large_votes)
{
	celerix::test::system system (1);
	auto & node = *system.nodes[0];

	const int count = 32;
	auto blocks = celerix::test::setup_chain (system, node, count, celerix::dev::genesis_key, /* do not confirm */ false);

	ASSERT_TRUE (celerix::test::start_elections (system, node, blocks));
	ASSERT_TIMELY (5s, celerix::test::active (node, blocks));

	auto vote = celerix::test::make_final_vote (celerix::dev::genesis_key, blocks);
	ASSERT_EQ (vote->hashes.size (), count);

	node.vote_processor.vote (vote, celerix::test::fake_channel (node));

	ASSERT_TIMELY (5s, celerix::test::confirmed (node, blocks));
}

/**
 * basic test to check that the timestamp mask is applied correctly on vote timestamp and duration fields
 */
TEST (vote, timestamp_and_duration_masking)
{
	celerix::test::system system;
	celerix::keypair key;
	auto hash = std::vector<celerix::block_hash>{ celerix::dev::genesis->hash () };
	auto vote = std::make_shared<celerix::vote> (key.pub, key.prv, 0x123f, 0xf, hash);
	ASSERT_EQ (vote->timestamp (), 0x1230);
	ASSERT_EQ (vote->duration ().count (), 524288);
	ASSERT_EQ (vote->duration_bits (), 0xf);
}

/**
 * Test that a vote can encode an empty hash set
 */
TEST (vote, empty_hashes)
{
	celerix::keypair key;
	auto vote = std::make_shared<celerix::vote> (key.pub, key.prv, 0, 0, std::vector<celerix::block_hash>{} /* empty */);
}
