#include <nano/lib/blocks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_rebroadcaster.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

/*
 * vote_rebroadcaster_index tests
 */

namespace
{
struct test_context
{
	nano::vote_rebroadcaster_config config;
	nano::vote_rebroadcaster_index index;

	explicit test_context (nano::vote_rebroadcaster_config config_a = {}) :
		config{ config_a },
		index{ config }
	{
	}
};
}

TEST (vote_rebroadcaster_index, construction)
{
	test_context ctx{};
	auto & index = ctx.index;
	ASSERT_EQ (index.representatives_count (), 0);
	ASSERT_EQ (index.total_history (), 0);
	ASSERT_EQ (index.total_hashes (), 0);
}

TEST (vote_rebroadcaster_index, basic_vote_tracking)
{
	test_context ctx{};
	auto & index = ctx.index;
	auto now = std::chrono::steady_clock::now ();

	nano::keypair key;
	std::vector<nano::block_hash> hashes = { nano::block_hash{ 1 } };
	auto vote = nano::test::make_vote (key, hashes);

	auto result = index.check_and_record (vote, nano::uint128_t{ 100 }, now);

	ASSERT_EQ (result, nano::vote_rebroadcaster_index::result::ok);
	ASSERT_EQ (index.representatives_count (), 1);
	ASSERT_EQ (index.total_history (), 1);
	ASSERT_EQ (index.total_hashes (), 1);
	ASSERT_TRUE (index.contains_representative (key.pub));
	ASSERT_TRUE (index.contains_block (key.pub, hashes[0]));
}

TEST (vote_rebroadcaster_index, duplicate_vote_rejection)
{
	test_context ctx{};
	auto & index = ctx.index;
	auto now = std::chrono::steady_clock::now ();

	nano::keypair key;
	std::vector<nano::block_hash> hashes = { nano::block_hash{ 1 } };
	auto vote = nano::test::make_vote (key, hashes);

	// First vote should be accepted
	auto result1 = index.check_and_record (vote, nano::uint128_t{ 100 }, now);
	ASSERT_EQ (result1, nano::vote_rebroadcaster_index::result::ok);

	// Same vote should be rejected
	auto result2 = index.check_and_record (vote, nano::uint128_t{ 100 }, now);
	ASSERT_EQ (result2, nano::vote_rebroadcaster_index::result::already_rebroadcasted);

	// Even after time threshold
	auto result3 = index.check_and_record (vote, nano::uint128_t{ 100 }, now + 60min);
	ASSERT_EQ (result3, nano::vote_rebroadcaster_index::result::already_rebroadcasted);
}

TEST (vote_rebroadcaster_index, rebroadcast_timing)
{
	nano::vote_rebroadcaster_config config;
	config.rebroadcast_threshold = 1000ms;
	test_context ctx{ config };
	auto & index = ctx.index;
	auto now = std::chrono::steady_clock::now ();

	nano::keypair key;
	std::vector<nano::block_hash> hashes = { nano::block_hash{ 1 } };

	// Initial vote
	auto vote1 = nano::test::make_vote (key, hashes, 1000);
	auto result1 = index.check_and_record (vote1, nano::uint128_t{ 100 }, now);
	ASSERT_EQ (result1, nano::vote_rebroadcaster_index::result::ok);

	// Try rebroadcast immediately - should be rejected
	auto vote2 = nano::test::make_vote (key, hashes, 1500);
	auto result2 = index.check_and_record (vote2, nano::uint128_t{ 100 }, now);
	ASSERT_EQ (result2, nano::vote_rebroadcaster_index::result::rebroadcast_unnecessary);

	// Try after threshold - should be accepted
	auto vote3 = nano::test::make_vote (key, hashes, 2500);
	auto result3 = index.check_and_record (vote3, nano::uint128_t{ 100 }, now + 2000ms);
	ASSERT_EQ (result3, nano::vote_rebroadcaster_index::result::ok);
}

TEST (vote_rebroadcaster_index, final_vote_override)
{
	test_context ctx{};
	auto & index = ctx.index;
	auto now = std::chrono::steady_clock::now ();

	nano::keypair key;
	std::vector<nano::block_hash> hashes = { nano::block_hash{ 1 } };

	// Regular vote
	auto vote1 = nano::test::make_vote (key, hashes, 1000);
	auto result1 = index.check_and_record (vote1, nano::uint128_t{ 100 }, now);
	ASSERT_EQ (result1, nano::vote_rebroadcaster_index::result::ok);

	// Final vote should override timing restrictions
	auto final_vote = nano::test::make_final_vote (key, hashes);
	auto result2 = index.check_and_record (final_vote, nano::uint128_t{ 100 }, now);
	ASSERT_EQ (result2, nano::vote_rebroadcaster_index::result::ok);

	// Both vote should be kept in recent hashes index
	ASSERT_EQ (index.total_history (), 1);
	ASSERT_EQ (index.total_hashes (), 2);
	ASSERT_TRUE (index.contains_block (key.pub, hashes[0]));
	ASSERT_TRUE (index.contains_vote (vote1->full_hash ()));
	ASSERT_TRUE (index.contains_vote (final_vote->full_hash ()));
}

TEST (vote_rebroadcaster_index, representative_limit)
{
	nano::vote_rebroadcaster_config config;
	config.max_representatives = 2;
	test_context ctx{ config };
	auto & index = ctx.index;
	auto now = std::chrono::steady_clock::now ();

	std::vector<nano::keypair> keys (4);
	std::vector<nano::block_hash> hashes = { nano::block_hash{ 1 } };

	// Add first rep (weight 100)
	auto vote1 = nano::test::make_vote (keys[0], hashes);
	auto result1 = index.check_and_record (vote1, nano::uint128_t{ 100 }, now);
	ASSERT_EQ (result1, nano::vote_rebroadcaster_index::result::ok);
	ASSERT_EQ (index.representatives_count (), 1);

	// Add second rep (weight 200)
	auto vote2 = nano::test::make_vote (keys[1], hashes);
	auto result2 = index.check_and_record (vote2, nano::uint128_t{ 200 }, now);
	ASSERT_EQ (result2, nano::vote_rebroadcaster_index::result::ok);
	ASSERT_EQ (index.representatives_count (), 2);

	// Try to add third rep with lower weight - should be rejected
	auto vote3 = nano::test::make_vote (keys[2], hashes);
	auto result3 = index.check_and_record (vote3, nano::uint128_t{ 50 }, now);
	ASSERT_EQ (result3, nano::vote_rebroadcaster_index::result::representatives_full);
	ASSERT_EQ (index.representatives_count (), 2);

	// Add third rep with higher weight - should replace lowest weight
	auto vote4 = nano::test::make_vote (keys[3], hashes);
	auto result4 = index.check_and_record (vote4, nano::uint128_t{ 300 }, now);
	ASSERT_EQ (result4, nano::vote_rebroadcaster_index::result::ok);
	ASSERT_FALSE (index.contains_representative (keys[0].pub)); // Lowest weight was removed
	ASSERT_EQ (index.representatives_count (), 2);
}

TEST (vote_rebroadcaster_index, multi_hash_vote)
{
	test_context ctx{};
	auto & index = ctx.index;
	auto now = std::chrono::steady_clock::now ();

	nano::keypair key;
	std::vector<nano::block_hash> hashes = {
		nano::block_hash{ 1 },
		nano::block_hash{ 2 },
		nano::block_hash{ 3 }
	};

	auto vote = nano::test::make_vote (key, hashes);
	auto result = index.check_and_record (vote, nano::uint128_t{ 100 }, now);

	ASSERT_EQ (result, nano::vote_rebroadcaster_index::result::ok);
	ASSERT_EQ (index.total_history (), 3); // One entry per hash
	for (auto const & hash : hashes)
	{
		ASSERT_TRUE (index.contains_block (key.pub, hash));
	}
}

TEST (vote_rebroadcaster_index, history_limit)
{
	nano::vote_rebroadcaster_config config;
	config.max_history = 2;
	test_context ctx{ config };
	auto & index = ctx.index;
	auto now = std::chrono::steady_clock::now ();

	nano::keypair key;

	// Add votes up to limit
	for (size_t i = 0; i < 3; i++)
	{
		std::vector<nano::block_hash> hash = { nano::block_hash{ i } };
		auto vote = nano::test::make_vote (key, hash);
		index.check_and_record (vote, nano::uint128_t{ 100 }, now);
	}

	ASSERT_EQ (index.total_history (), 2);
	ASSERT_FALSE (index.contains_block (key.pub, nano::block_hash{ 0 })); // Oldest was removed
	ASSERT_TRUE (index.contains_block (key.pub, nano::block_hash{ 1 }));
	ASSERT_TRUE (index.contains_block (key.pub, nano::block_hash{ 2 }));
}

TEST (vote_rebroadcaster_index, cleanup)
{
	test_context ctx{};
	auto & index = ctx.index;
	auto now = std::chrono::steady_clock::now ();

	nano::keypair key1;
	nano::keypair key2;
	std::vector<nano::block_hash> hashes = { nano::block_hash{ 1 } };

	// Add two reps
	auto vote1 = nano::test::make_vote (key1, hashes);
	auto vote2 = nano::test::make_vote (key2, hashes);
	index.check_and_record (vote1, nano::uint128_t{ 100 }, now);
	index.check_and_record (vote2, nano::uint128_t{ 200 }, now);

	// Cleanup with rep1 becoming non-principal
	auto cleanup_count = index.cleanup ([&key1] (nano::account const & account) {
		return std::make_pair (
		account == key1.pub ? false : true,
		account == key1.pub ? nano::uint128_t{ 0 } : nano::uint128_t{ 200 });
	});

	ASSERT_EQ (cleanup_count, 1);
	ASSERT_EQ (index.representatives_count (), 1);
	ASSERT_FALSE (index.contains_representative (key1.pub));
	ASSERT_TRUE (index.contains_representative (key2.pub));
}

TEST (vote_rebroadcaster_index, weight_updates)
{
	nano::vote_rebroadcaster_config config;
	config.max_representatives = 1;
	test_context ctx{ config };
	auto & index = ctx.index;
	auto now = std::chrono::steady_clock::now ();

	nano::keypair key1;
	nano::keypair key2;
	std::vector<nano::block_hash> hashes = { nano::block_hash{ 1 } };

	// Add rep with initial weight
	auto vote1 = nano::test::make_vote (key1, hashes);
	index.check_and_record (vote1, nano::uint128_t{ 100 }, now);

	// Update weight through cleanup
	index.cleanup ([] (nano::account const &) {
		return std::make_pair (true, nano::uint128_t{ 200 });
	});

	// Add new rep with lower weight - should be rejected due to updated weight
	auto vote2 = nano::test::make_vote (key2, hashes);
	auto result = index.check_and_record (vote2, nano::uint128_t{ 150 }, now);
	ASSERT_EQ (result, nano::vote_rebroadcaster_index::result::representatives_full);
}

/*
 * vote_rebroadcaster tests
 */

// Verifies that external votes are queued and rebroadcasted
TEST (vote_rebroadcaster, basic_rebroadcast)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan.enable = false;
	config.hinted_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;

	// Node without rep key in wallet - will act as rebroadcaster
	auto & node = *system.add_node (config);

	// Add a peer node to receive rebroadcasts
	auto & peer_node = *system.add_node (config);

	// Create a block and start an election
	auto block = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false).front ();

	// Start election for the block
	ASSERT_TRUE (nano::test::start_election (system, node, block->hash ()));

	// Genesis should be viewed as a principal representative
	ASSERT_NE (nano::rep_tier::none, node.rep_tiers.tier (nano::dev::genesis_key.pub));

	// Create a vote from genesis (principal representative)
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { block->hash () });

	// Clear stats
	node.stats.clear ();

	// Process the vote (simulates receiving vote from network)
	node.process_active (vote);

	// Wait for vote to be processed and queued for rebroadcast
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::queued) > 0);

	// Verify processing and rebroadcast stats
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::process) > 0);
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::rebroadcast) > 0);
}

// Verifies that votes from local wallet representatives are not rebroadcasted
TEST (vote_rebroadcaster, local_representative_votes_skipped)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan.enable = false;
	config.hinted_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;

	auto & node = *system.add_node (config);
	auto & peer_node = *system.add_node (config); // Needed to allow rebroadcasting

	// Insert genesis key into wallet - making it a local representative
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Wait for wallet reps to be computed
	ASSERT_TIMELY (5s, node.wallets.reps ().exists (nano::dev::genesis_key.pub));

	// Create a block and start an election
	auto block = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false).front ();
	ASSERT_TRUE (nano::test::start_election (system, node, block->hash ()));

	// Create a vote from genesis (now a local representative)
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { block->hash () });

	// Clear stats and wait for refresh interval to elapse so local rep cache is updated
	node.stats.clear ();
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::refresh) > 0);
	node.stats.clear ();

	// Process the vote
	node.process_active (vote);

	// Verify the vote was not queued for rebroadcast (local rep votes are skipped)
	ASSERT_ALWAYS_EQ (250ms, 0, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::queued));
}

// Verifies that rebroadcasting is disabled when node hosts a principal representative
TEST (vote_rebroadcaster, disabled_when_principal_representative)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan.enable = false;
	config.hinted_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;

	auto & node = *system.add_node (config);
	auto & peer_node = *system.add_node (config); // Needed to allow rebroadcasting

	// Insert genesis key into wallet - this makes the node host a principal representative
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Wait for principal status to be detected
	ASSERT_TIMELY (5s, node.wallets.reps ().have_half_rep ());

	// Wait for rebroadcaster to refresh and detect has_principal
	node.stats.clear ();
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::refresh) > 1);

	// Create a different representative key with enough weight to be tier 1+
	nano::keypair other_rep;
	auto const send_amount = nano::Knano_ratio * 1000;

	// Send some funds to other_rep to give it weight
	auto send = nano::state_block_builder ()
				.account (nano::dev::genesis_key.pub)
				.representative (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.link (other_rep.pub)
				.balance (nano::dev::constants.genesis_amount - send_amount)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	// Open account and set self as representative
	auto open = nano::state_block_builder ()
				.account (other_rep.pub)
				.representative (other_rep.pub)
				.previous (0)
				.link (send->hash ())
				.balance (send_amount)
				.sign (other_rep.prv, other_rep.pub)
				.work (*system.work.generate (other_rep.pub))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open));

	// Start election for a block
	ASSERT_TRUE (nano::test::start_election (system, node, send->hash ()));

	// Wait for rep tiers to recognize other_rep
	ASSERT_TIMELY (10s, node.rep_tiers.tier (other_rep.pub) != nano::rep_tier::none);

	// Create a vote from other_rep (external principal representative)
	auto vote = nano::test::make_vote (other_rep, { send->hash () });

	// Process the vote (should not be queued)
	node.process_active (vote);

	// Verify no votes were queued
	ASSERT_ALWAYS_EQ (250ms, 0, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::queued));
}

// Verifies that votes from non-principal representatives are not queued
TEST (vote_rebroadcaster, non_principal_rep_votes_rejected)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan.enable = false;
	config.hinted_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;

	auto & node = *system.add_node (config);
	auto & peer_node = *system.add_node (config); // Needed to allow rebroadcasting

	// Create a block and start election
	auto block = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false).front ();
	ASSERT_TRUE (nano::test::start_election (system, node, block->hash ()));

	// Create a vote from a random keypair with no weight (non-principal)
	nano::keypair random_key;
	ASSERT_EQ (nano::rep_tier::none, node.rep_tiers.tier (random_key.pub));

	auto vote = nano::test::make_vote (random_key, { block->hash () });

	// Clear stats
	node.stats.clear ();

	// Process the vote
	node.process_active (vote);

	// Wait for vote to be processed
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::vote_processor, nano::stat::detail::process) > 0);

	// Verify the vote was not queued (non-principal rep)
	ASSERT_ALWAYS_EQ (250ms, 0, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::queued));
}

// Verifies that the same vote signature is not queued multiple times
TEST (vote_rebroadcaster, duplicate_vote_ignored)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan.enable = false;
	config.hinted_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;

	auto & node = *system.add_node (config);
	// No peer node, vote rebroadcaster should not drain the queue

	// Create a block and start election
	auto block = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false).front ();
	ASSERT_TRUE (nano::test::start_election (system, node, block->hash ()));

	// Create a vote from genesis
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { block->hash () });

	// Clear stats
	node.stats.clear ();

	// Push the same vote multiple times directly to rebroadcaster
	auto tier = node.rep_tiers.tier (nano::dev::genesis_key.pub);
	ASSERT_NE (nano::rep_tier::none, tier);

	bool first_push = node.vote_rebroadcaster.push (vote, tier);
	bool second_push = node.vote_rebroadcaster.push (vote, tier);
	bool third_push = node.vote_rebroadcaster.push (vote, tier);

	// First push should succeed, subsequent pushes should fail (duplicate signature)
	ASSERT_TRUE (first_push);
	ASSERT_FALSE (second_push);
	ASSERT_FALSE (third_push);

	// Only one queued stat
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::queued));
}

// Verifies that vote rebroadcasting is disabled when config.enable = false
TEST (vote_rebroadcaster, disabled)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.vote_rebroadcaster.enable = false; // Disable rebroadcaster
	config.backlog_scan.enable = false;
	config.hinted_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;

	auto & node = *system.add_node (config);
	auto & peer_node = *system.add_node (config); // Needed to allow rebroadcasting

	// Create a block and start election
	auto block = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false).front ();
	ASSERT_TRUE (nano::test::start_election (system, node, block->hash ()));

	// Create a vote from genesis
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { block->hash () });

	// Clear stats
	node.stats.clear ();

	// Process the vote
	node.process_active (vote);

	// Verify no votes were queued (rebroadcaster is disabled)
	ASSERT_ALWAYS_EQ (250ms, 0, node.stats.count (nano::stat::type::vote_rebroadcaster, nano::stat::detail::queued));
}