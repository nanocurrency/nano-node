#include <nano/node/election.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

/*
 * Ensure account gets activated for a single unconfirmed account chain
 */
TEST (optimistic_scheduler, activate_one)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	// Needs to be greater than optimistic scheduler `gap_threshold`
	const int howmany_blocks = 64;

	auto chains = nano::test::setup_chains (system, node, /* single chain */ 1, howmany_blocks, nano::dev::genesis_key, /* do not confirm */ false);
	auto & [account, blocks] = chains.front ();

	// Confirm block towards at the beginning the chain, so gap between confirmation and account frontier is larger than `gap_threshold`
	ASSERT_TRUE (nano::test::start_elections (system, node, { blocks.at (11) }, true));
	ASSERT_TIMELY (5s, nano::test::confirmed (node, { blocks.at (11) }));

	// Ensure unconfirmed account head block gets activated
	auto const & block = blocks.back ();
	ASSERT_TIMELY (5s, node.active.active (block->hash ()));
	ASSERT_TRUE (node.active.election (block->qualified_root ())->behavior () == nano::election_behavior::optimistic);
}

/*
 * Ensure account gets activated for a single unconfirmed account chain with nothing yet confirmed
 */
TEST (optimistic_scheduler, activate_one_zero_conf)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	// Can be smaller than optimistic scheduler `gap_threshold`
	// This is meant to activate short account chains (eg. binary tree spam leaf accounts)
	const int howmany_blocks = 6;

	auto chains = nano::test::setup_chains (system, node, /* single chain */ 1, howmany_blocks, nano::dev::genesis_key, /* do not confirm */ false);
	auto & [account, blocks] = chains.front ();

	// Ensure unconfirmed account head block gets activated
	auto const & block = blocks.back ();
	ASSERT_TIMELY (5s, node.active.active (block->hash ()));
	ASSERT_TRUE (node.active.election (block->qualified_root ())->behavior () == nano::election_behavior::optimistic);
}

/*
 * Ensure account gets activated for a multiple unconfirmed account chains
 */
TEST (optimistic_scheduler, activate_many)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	// Needs to be greater than optimistic scheduler `gap_threshold`
	const int howmany_blocks = 64;
	const int howmany_chains = 16;

	auto chains = nano::test::setup_chains (system, node, howmany_chains, howmany_blocks, nano::dev::genesis_key, /* do not confirm */ false);

	// Ensure all unconfirmed accounts head block gets activated
	ASSERT_TIMELY (5s, std::all_of (chains.begin (), chains.end (), [&] (auto const & entry) {
		auto const & [account, blocks] = entry;
		auto const & block = blocks.back ();
		return node.active.active (block->hash ()) && node.active.election (block->qualified_root ())->behavior () == nano::election_behavior::optimistic;
	}));
}

/*
 * Ensure accounts with some blocks already confirmed and with less than `gap_threshold` blocks do not get activated
 */
TEST (optimistic_scheduler, under_gap_threshold)
{
	nano::test::system system{};
	nano::node_config config = system.default_config ();
	config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config);

	// Must be smaller than optimistic scheduler `gap_threshold`
	const int howmany_blocks = 64;

	auto chains = nano::test::setup_chains (system, node, /* single chain */ 1, howmany_blocks, nano::dev::genesis_key, /* do not confirm */ false);
	auto & [account, blocks] = chains.front ();

	// Confirm block towards the end of the chain, so gap between confirmation and account frontier is less than `gap_threshold`
	ASSERT_TRUE (nano::test::start_elections (system, node, { blocks.at (55) }, true));
	ASSERT_TIMELY (5s, nano::test::confirmed (node, { blocks.at (55) }));

	// Manually trigger backlog scan
	node.backlog.trigger ();

	// Ensure unconfirmed account head block gets activated
	auto const & block = blocks.back ();
	ASSERT_NEVER (3s, node.active.active (block->hash ()));
}
