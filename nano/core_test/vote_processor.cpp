#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/election.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (vote_processor, codes)
{
	nano::test::system system;
	auto node_config = system.default_config ();
	// Disable all election schedulers
	node_config.backlog_scan->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	auto & node = *system.add_node (node_config);

	auto blocks = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false);
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { blocks[0] }, nano::vote::timestamp_min * 1, 0);
	auto vote_invalid = std::make_shared<nano::vote> (*vote);
	vote_invalid->signature.bytes[0] ^= 1;
	auto channel (std::make_shared<nano::transport::inproc::channel> (node, node));

	// Invalid signature
	ASSERT_EQ (nano::vote_code::invalid, node.vote_processor.vote_blocking (vote_invalid, channel));

	// No ongoing election (vote goes to vote cache)
	ASSERT_EQ (nano::vote_code::indeterminate, node.vote_processor.vote_blocking (vote, channel));

	// Clear vote cache before starting election
	node.vote_cache.clear ();

	// First vote from an account for an ongoing election
	node.start_election (blocks[0]);
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (blocks[0]->qualified_root ()));
	ASSERT_EQ (nano::vote_code::vote, node.vote_processor.vote_blocking (vote, channel));

	// Processing the same vote is a replay
	ASSERT_EQ (nano::vote_code::replay, node.vote_processor.vote_blocking (vote, channel));

	// Invalid takes precedence
	ASSERT_EQ (nano::vote_code::invalid, node.vote_processor.vote_blocking (vote_invalid, channel));

	// Once the election is removed (confirmed / dropped) the vote is again indeterminate
	ASSERT_TRUE (node.active.erase (blocks[0]->qualified_root ()));
	ASSERT_EQ (nano::vote_code::indeterminate, node.vote_processor.vote_blocking (vote, channel));
}

TEST (vote_processor, invalid_signature)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	auto chain = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false);
	nano::keypair key;
	auto vote = std::make_shared<nano::vote> (key.pub, key.prv, nano::vote::timestamp_min * 1, 0, std::vector<nano::block_hash>{ chain[0]->hash () });
	auto vote_invalid = std::make_shared<nano::vote> (*vote);
	vote_invalid->signature.bytes[0] ^= 1;
	auto channel = std::make_shared<nano::transport::inproc::channel> (node, node);

	auto election = nano::test::start_election (system, node, chain[0]->hash ());
	ASSERT_NE (election, nullptr);
	ASSERT_EQ (0, election->votes ().size ());

	node.vote_processor.vote (vote_invalid, channel);
	ASSERT_TIMELY_EQ (5s, 0, election->votes ().size ());
	node.vote_processor.vote (vote, channel);
	ASSERT_TIMELY_EQ (5s, 1, election->votes ().size ());
}

TEST (vote_processor, overflow)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.vote_processor_capacity = 1;
	auto & node (*system.add_node (node_flags));
	nano::keypair key;
	auto vote = nano::test::make_vote (key, { nano::dev::genesis }, nano::vote::timestamp_min * 1, 0);
	auto channel (std::make_shared<nano::transport::inproc::channel> (node, node));
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
	ASSERT_EQ (not_processed, node.stats.count (nano::stat::type::vote_processor, nano::stat::detail::overfill));

	// check that it did not timeout
	ASSERT_LT (std::chrono::system_clock::now () - start_time, 10s);
}

TEST (vote_processor, weights)
{
	nano::test::system system (4);
	auto & node (*system.nodes[0]);

	// Create representatives of different weight levels
	auto const stake = nano::dev::genesis->balance ().number ();
	auto const level0 = stake / 5000; // 0.02%
	auto const level1 = stake / 500; // 0.2%
	auto const level2 = stake / 50; // 2%

	nano::keypair key0;
	nano::keypair key1;
	nano::keypair key2;

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key0.prv);
	system.wallet (2)->insert_adhoc (key1.prv);
	system.wallet (3)->insert_adhoc (key2.prv);
	system.wallet (1)->set_representative (key0.pub);
	system.wallet (2)->set_representative (key1.pub);
	system.wallet (3)->set_representative (key2.pub);
	system.wallet (0)->send_sync (nano::dev::genesis_key.pub, key0.pub, level0);
	system.wallet (0)->send_sync (nano::dev::genesis_key.pub, key1.pub, level1);
	system.wallet (0)->send_sync (nano::dev::genesis_key.pub, key2.pub, level2);

	// Wait for representatives
	ASSERT_TIMELY_EQ (10s, node.ledger.rep_weights.get_all ().size (), 4);

	// Wait for rep tiers to be updated
	node.stats.clear ();
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::rep_tiers, nano::stat::detail::updated) >= 2);

	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key0.pub), nano::rep_tier::none);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key1.pub), nano::rep_tier::tier_1);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key2.pub), nano::rep_tier::tier_2);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (nano::dev::genesis_key.pub), nano::rep_tier::tier_3);
}

// Verify that multiple votes from different accounts are processed and inserted into a single election
TEST (vote_processor, election)
{
	nano::test::system system;
	auto node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	node_config.priority_scheduler->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	auto & node = *system.add_node (node_config);

	auto blocks = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false);
	auto & block = blocks[0];

	auto election = nano::test::start_election (system, node, block->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (0, election->votes ().size ());

	// Create votes from multiple accounts
	nano::keypair key1, key2, key3;
	auto vote1 = nano::test::make_vote (key1, { block }, nano::vote::timestamp_min * 1, 0);
	auto vote2 = nano::test::make_vote (key2, { block }, nano::vote::timestamp_min * 2, 0);
	auto vote3 = nano::test::make_vote (key3, { block }, nano::vote::timestamp_min * 3, 0);

	auto channel = nano::test::fake_channel (node);

	// Process all votes
	node.vote_processor.vote (vote1, channel);
	node.vote_processor.vote (vote2, channel);
	node.vote_processor.vote (vote3, channel);

	// Verify all votes are inserted into the election
	ASSERT_TIMELY_EQ (5s, election->votes ().size (), 3);

	auto votes = election->votes ();
	ASSERT_TRUE (votes.contains (key1.pub));
	ASSERT_TRUE (votes.contains (key2.pub));
	ASSERT_TRUE (votes.contains (key3.pub));
	ASSERT_EQ (votes.at (key1.pub).timestamp, nano::vote::timestamp_min * 1);
	ASSERT_EQ (votes.at (key2.pub).timestamp, nano::vote::timestamp_min * 2);
	ASSERT_EQ (votes.at (key3.pub).timestamp, nano::vote::timestamp_min * 3);
}

// Verify that a vote with multiple hashes is routed to multiple elections
TEST (vote_processor, multiple_elections)
{
	nano::test::system system;
	auto node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	node_config.priority_scheduler->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	auto & node = *system.add_node (node_config);

	auto blocks = nano::test::setup_chain (system, node, 4, nano::dev::genesis_key, false);
	auto & block1 = blocks[0];
	auto & block2 = blocks[1];
	auto & block3 = blocks[2];
	auto & block4 = blocks[3];

	// Start elections for all blocks
	auto election1 = nano::test::start_election (system, node, block1->hash ());
	auto election2 = nano::test::start_election (system, node, block2->hash ());
	auto election3 = nano::test::start_election (system, node, block3->hash ());
	auto election4 = nano::test::start_election (system, node, block4->hash ());
	ASSERT_NE (nullptr, election1);
	ASSERT_NE (nullptr, election2);
	ASSERT_NE (nullptr, election3);
	ASSERT_NE (nullptr, election4);

	// Create a single vote containing all block hashes
	nano::keypair key;
	auto vote = nano::test::make_vote (key, { block1, block2, block3, block4 }, nano::vote::timestamp_min * 1, 0);
	ASSERT_EQ (vote->hashes.size (), 4);

	auto channel = nano::test::fake_channel (node);
	node.vote_processor.vote (vote, channel);

	// Verify the vote appears in each election
	ASSERT_TIMELY_EQ (5s, election1->votes ().size (), 1);
	ASSERT_TIMELY_EQ (5s, election2->votes ().size (), 1);
	ASSERT_TIMELY_EQ (5s, election3->votes ().size (), 1);
	ASSERT_TIMELY_EQ (5s, election4->votes ().size (), 1);

	ASSERT_TRUE (election1->votes ().contains (key.pub));
	ASSERT_TRUE (election2->votes ().contains (key.pub));
	ASSERT_TRUE (election3->votes ().contains (key.pub));
	ASSERT_TRUE (election4->votes ().contains (key.pub));
}

// Ensure that node behaves well with votes larger than 12 hashes, which was maximum before V26
TEST (vote_processor, large_votes)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	const int count = 32;
	auto blocks = nano::test::setup_chain (system, node, count, nano::dev::genesis_key, /* do not confirm */ false);

	ASSERT_TRUE (nano::test::start_elections (system, node, blocks));
	ASSERT_TIMELY (5s, nano::test::active (node, blocks));

	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, blocks);
	ASSERT_EQ (vote->hashes.size (), count);

	node.vote_processor.vote (vote, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, nano::test::confirmed (node, blocks));
}

/*
 * vote_cache_processor tests
 */

namespace
{
std::shared_ptr<nano::block> make_genesis_send (nano::test::system & system)
{
	nano::keypair key;
	return nano::state_block_builder ()
	.account (nano::dev::genesis_key.pub)
	.previous (nano::dev::genesis->hash ())
	.representative (nano::dev::genesis_key.pub)
	.balance (nano::dev::constants.genesis_amount - 1)
	.link (key.pub)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (*system.work.generate (nano::dev::genesis->hash ()))
	.build ();
}
}

// The context carries the vote together with the source and channel it arrived with
TEST (vote_processor, context_carries_origin)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// Any vote and any peer channel will do, the lookup only passes them through
	auto const vote = nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis });
	auto const channel = nano::test::fake_channel (node);

	// A rebroadcast vote keeps its channel and its source
	auto const context = node.vote_processor.context (vote, channel, nano::vote_source::rebroadcast);
	ASSERT_EQ (vote, context.vote);
	ASSERT_EQ (channel, context.channel);
	ASSERT_EQ (nano::vote_source::rebroadcast, context.source);

	// A vote replayed from the vote cache has no channel
	auto const cached = node.vote_processor.context (vote, nullptr, nano::vote_source::cache);
	ASSERT_EQ (vote, cached.vote);
	ASSERT_EQ (nullptr, cached.channel);
	ASSERT_EQ (nano::vote_source::cache, cached.source);
}

// The context holds the representative's ledger weight and the tier that weight earns against the trended online stake
TEST (vote_processor, context_weight_and_tier)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// Twice the principal minimum puts a representative in the lowest tier, genesis keeps the rest of the supply
	auto const amount = node.minimum_principal_weight () * 2;
	auto const rep = nano::test::setup_rep (system, node, amount);

	// The funded representative gets exactly the amount delegated to it, and its tier right away without waiting for the background tier refresh
	auto const funded = node.vote_processor.context (nano::test::make_vote (rep, { nano::dev::genesis }), nullptr, nano::vote_source::live);
	ASSERT_EQ (amount, funded.weight);
	ASSERT_EQ (nano::rep_tier::tier_1, funded.tier);

	// Genesis lost that amount to the funded representative
	auto const genesis = node.vote_processor.context (nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis }), nullptr, nano::vote_source::live);
	ASSERT_EQ (nano::dev::constants.genesis_amount - amount, genesis.weight);
	ASSERT_EQ (nano::rep_tier::tier_3, genesis.tier);

	// An account nobody delegated to has neither weight nor tier
	nano::keypair unfunded;
	auto const nobody = node.vote_processor.context (nano::test::make_vote (unfunded, { nano::dev::genesis }), nullptr, nano::vote_source::live);
	ASSERT_EQ (0, nobody.weight);
	ASSERT_EQ (nano::rep_tier::none, nobody.tier);
}

// A representative is principal only with weight strictly above the principal minimum, which is exactly where it gains a tier
TEST (vote_processor, context_principal_minimum)
{
	nano::test::system system;

	// Apply the real principal rule instead of the dev default that counts every representative
	nano::node_flags flags;
	flags.all_reps_principal = false;
	auto & node = *system.add_node (flags);

	// One representative sits exactly at the minimum, another one raw above it
	auto const minimum = node.minimum_principal_weight ();
	auto const at_minimum = nano::test::setup_rep (system, node, minimum);
	auto const above_minimum = nano::test::setup_rep (system, node, minimum + 1);
	nano::keypair unfunded;

	// Nothing votes, so the online stake the minimum derives from stays put
	ASSERT_EQ (minimum, node.minimum_principal_weight ());

	auto const context = [&node] (nano::keypair const & rep) {
		return node.vote_processor.context (nano::test::make_vote (rep, { nano::dev::genesis }), nullptr, nano::vote_source::live);
	};

	// Exactly the minimum is not enough, for principal status or for a tier
	ASSERT_EQ (minimum, context (at_minimum).weight);
	ASSERT_FALSE (context (at_minimum).principal);
	ASSERT_EQ (nano::rep_tier::none, context (at_minimum).tier);

	// One raw above the minimum earns both
	ASSERT_EQ (minimum + 1, context (above_minimum).weight);
	ASSERT_TRUE (context (above_minimum).principal);
	ASSERT_EQ (nano::rep_tier::tier_1, context (above_minimum).tier);

	// No weight is never principal, the whole remaining supply always is
	ASSERT_FALSE (context (unfunded).principal);
	ASSERT_TRUE (context (nano::dev::genesis_key).principal);
}

// With every representative counted as principal, a representative without weight is principal too, its weight and tier untouched
TEST (vote_processor, context_all_reps_principal)
{
	nano::test::system system;

	// Set the flag explicitly rather than relying on the dev default
	nano::node_flags flags;
	flags.all_reps_principal = true;
	auto & node = *system.add_node (flags);

	// An account nobody delegated to
	nano::keypair unfunded;
	auto const context = node.vote_processor.context (nano::test::make_vote (unfunded, { nano::dev::genesis }), nullptr, nano::vote_source::live);

	// The flag makes it principal
	ASSERT_TRUE (context.principal);

	// But it still reports the real weight and tier
	ASSERT_EQ (0, context.weight);
	ASSERT_EQ (nano::rep_tier::none, context.tier);
}

// Below the bootstrap height the context takes a representative's preconfigured bootstrap weight, and judges its principal status and tier by that weight
TEST (vote_processor, context_bootstrap_weight)
{
	nano::test::system system;

	// Apply the real principal rule, so the bootstrap weight has to earn the principal status
	nano::node_flags flags;
	flags.all_reps_principal = false;

	// Never started, so no background thread reads the bootstrap table while the test fills it
	auto node = std::make_shared<nano::node> (system.get_available_port (), nano::unique_path (), system.work, flags);
	system.register_node (node);

	// Keep the ledger below the bootstrap height and preconfigure a representative just above the principal minimum
	nano::keypair rep;
	auto const weight = node->minimum_principal_weight () + 1;
	node->ledger.bootstrap_weights.max_blocks = node->ledger.block_count () + 1;
	node->ledger.bootstrap_weights.representatives[rep.pub] = weight;

	// The preconfigured representative gets its bootstrap weight, which makes it principal with the lowest tier
	auto const bootstrapped = node->vote_processor.context (nano::test::make_vote (rep, { nano::dev::genesis }), nullptr, nano::vote_source::live);
	ASSERT_EQ (weight, bootstrapped.weight);
	ASSERT_TRUE (bootstrapped.principal);
	ASSERT_EQ (nano::rep_tier::tier_1, bootstrapped.tier);

	// The ledger's own weight does not count until the bootstrap height is reached, not even for genesis
	auto const genesis = node->vote_processor.context (nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis }), nullptr, nano::vote_source::live);
	ASSERT_EQ (0, genesis.weight);
	ASSERT_FALSE (genesis.principal);
	ASSERT_EQ (nano::rep_tier::none, genesis.tier);
}

// A vote cached before its election started reaches the election once it starts
TEST (vote_cache_processor, delivers_cached_vote)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto send = make_genesis_send (system);

	// Cached before the block reaches the ledger, so any election for it starts after the vote is cached
	node.vote_cache.insert (nano::test::make_vote (nano::dev::genesis_key, { send }), 0 /* any weight */);
	ASSERT_TRUE (nano::test::process (node, { send }));
	auto election = nano::test::start_election (system, node, send->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_TIMELY (5s, election->votes ().contains (nano::dev::genesis_key.pub));
}

// A disabled processor never looks up the hashes elections queue when they start, so a cached vote never reaches its election
TEST (vote_cache_processor, disabled)
{
	nano::test::system system;
	auto config = system.default_config ();
	config.vote_cache_processor->enable = false;
	auto & node = *system.add_node (config);
	auto send = make_genesis_send (system);

	// Cached before the block reaches the ledger, so any election for it starts after the vote is cached
	node.vote_cache.insert (nano::test::make_vote (nano::dev::genesis_key, { send }), 0 /* any weight */);
	ASSERT_TRUE (nano::test::process (node, { send }));
	auto election = nano::test::start_election (system, node, send->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (node.vote_cache_processor.empty ());
	ASSERT_ALWAYS_EQ (1s, node.stats.count (nano::stat::type::vote_cache_processor, nano::stat::detail::processed), 0);
	ASSERT_FALSE (election->votes ().contains (nano::dev::genesis_key.pub));
}
