#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/local_block_broadcaster.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace
{
// Flags for a node whose blocks nothing but the local block broadcaster floods
nano::node_flags quiet_flags ()
{
	nano::node_flags flags;
	flags.disable_elections = true;
	return flags;
}

// `count` sends of one raw each from the genesis account to `destination`, chained after `previous` which holds `balance`
std::vector<std::shared_ptr<nano::block>> genesis_sends (nano::test::system & system, nano::block_hash previous, nano::uint128_t balance, nano::account destination, size_t count)
{
	nano::block_builder builder;
	std::vector<std::shared_ptr<nano::block>> blocks;
	for (size_t n = 0; n < count; ++n)
	{
		balance -= 1;
		auto block = builder.state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (previous)
					 .representative (nano::dev::genesis_key.pub)
					 .balance (balance)
					 .link (destination)
					 .work (*system.work.generate (previous))
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .build ();
		previous = block->hash ();
		blocks.push_back (block);
	}
	return blocks;
}

size_t count (nano::node & node, nano::stat::detail detail)
{
	return node.stats.count (nano::stat::type::local_block_broadcaster, detail);
}
}

/*
 * Only blocks processed as local are tracked, a block that fails processing or arrives any other way is not
 */
TEST (local_block_broadcaster, tracks_local_blocks_only)
{
	nano::test::system system;
	auto & node = *system.add_node (quiet_flags ());

	auto sends = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 1 }, 4);

	// A local block is tracked once it has been processed
	ASSERT_EQ (nano::block_status::progress, node.process_local (sends[0]).value ());
	ASSERT_TIMELY (5s, node.local_block_broadcaster.contains (sends[0]->hash ()));
	ASSERT_EQ (node.local_block_broadcaster.size (), 1);

	// Processing it again, a block that fails, a block processed directly and a block received from the network add nothing
	ASSERT_EQ (nano::block_status::old, node.process_local (sends[0]).value ());
	ASSERT_EQ (nano::block_status::gap_previous, node.process_local (sends[2]).value ());
	ASSERT_EQ (nano::block_status::progress, node.process (sends[1]));
	node.process_active (sends[2]);
	ASSERT_TIMELY (5s, node.block (sends[2]->hash ()));

	// Tracking trails processing, so a further local block marks the point where everything above has been seen
	ASSERT_EQ (nano::block_status::progress, node.process_local (sends[3]).value ());
	ASSERT_TIMELY (5s, node.local_block_broadcaster.contains (sends[3]->hash ()));
	ASSERT_FALSE (node.local_block_broadcaster.contains (sends[1]->hash ()));
	ASSERT_FALSE (node.local_block_broadcaster.contains (sends[2]->hash ()));
	ASSERT_EQ (node.local_block_broadcaster.size (), 2);
	ASSERT_EQ (count (node, nano::stat::detail::insert), 2);
}

/*
 * Beyond `max_size` the oldest tracked blocks make room for new ones
 */
TEST (local_block_broadcaster, evicts_oldest_beyond_max_size)
{
	nano::test::system system;
	auto config = system.default_config ();
	config.local_block_broadcaster->max_size = 3;
	auto & node = *system.add_node (config, quiet_flags ());

	auto sends = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 1 }, 5);
	for (auto const & block : sends)
	{
		ASSERT_EQ (nano::block_status::progress, node.process_local (block).value ());
	}
	ASSERT_TIMELY (5s, node.local_block_broadcaster.contains (sends.back ()->hash ()));

	// The two oldest are gone, the three newest remain
	ASSERT_EQ (node.local_block_broadcaster.size (), 3);
	ASSERT_FALSE (node.local_block_broadcaster.contains (sends[0]->hash ()));
	ASSERT_FALSE (node.local_block_broadcaster.contains (sends[1]->hash ()));
	ASSERT_TRUE (node.local_block_broadcaster.contains (sends[2]->hash ()));
	ASSERT_TRUE (node.local_block_broadcaster.contains (sends[4]->hash ()));
	ASSERT_EQ (count (node, nano::stat::detail::erase_oldest), 2);
}

/*
 * A tracked block that a forced fork rolls back is dropped and not broadcast again
 */
TEST (local_block_broadcaster, rollback_erases_entry)
{
	nano::test::system system;
	auto & node = *system.add_node (quiet_flags ());

	// Two sends from the same previous, forcing the second one rolls the first one back
	auto send = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 1 }, 1).front ();
	auto fork = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 2 }, 1).front ();
	ASSERT_EQ (nano::block_status::progress, node.process_local (send).value ());
	ASSERT_TIMELY (5s, node.local_block_broadcaster.contains (send->hash ()));
	ASSERT_TIMELY (5s, count (node, nano::stat::detail::broadcast) >= 1);

	node.block_processor.force (fork);
	ASSERT_TIMELY (5s, node.block (fork->hash ()));
	ASSERT_TIMELY (5s, !node.local_block_broadcaster.contains (send->hash ()));
	ASSERT_EQ (count (node, nano::stat::detail::rollback), 1);

	// The forced block was not local, so nothing is left to rebroadcast
	ASSERT_FALSE (node.local_block_broadcaster.contains (fork->hash ()));
	ASSERT_EQ (node.local_block_broadcaster.size (), 0);
	auto const broadcasts = count (node, nano::stat::detail::broadcast);
	ASSERT_ALWAYS_EQ (2s, count (node, nano::stat::detail::broadcast), broadcasts);
}

/*
 * A tracked block that gets cemented is dropped and not broadcast again
 */
TEST (local_block_broadcaster, cemented_erases_entry)
{
	nano::test::system system;
	auto & node = *system.add_node (quiet_flags ());

	auto send = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 1 }, 1).front ();
	ASSERT_EQ (nano::block_status::progress, node.process_local (send).value ());
	ASSERT_TIMELY (5s, count (node, nano::stat::detail::broadcast) >= 1);

	node.cementing_set.add (send->hash ());
	ASSERT_TIMELY (5s, !node.local_block_broadcaster.contains (send->hash ()));
	ASSERT_EQ (count (node, nano::stat::detail::cemented), 1);
	ASSERT_EQ (node.local_block_broadcaster.size (), 0);
	auto const broadcasts = count (node, nano::stat::detail::broadcast);
	ASSERT_ALWAYS_EQ (2s, count (node, nano::stat::detail::broadcast), broadcasts);
}

/*
 * Cleanup drops a tracked block once it is confirmed, but only after it has been broadcast at least once
 */
TEST (local_block_broadcaster, cleanup_keeps_confirmed_until_broadcast)
{
	nano::test::system system;
	auto config = system.default_config ();
	config.local_block_broadcaster->broadcast_rate_limit = 1;
	config.local_block_broadcaster->broadcast_rate_burst_ratio = 0.5; // A single token, so the second block waits a second for its first flood
	auto & node = *system.add_node (config, quiet_flags ());

	auto sends = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 1 }, 2);
	ASSERT_EQ (nano::block_status::progress, node.process_local (sends[0]).value ());
	ASSERT_EQ (nano::block_status::progress, node.process_local (sends[1]).value ());
	ASSERT_TIMELY (5s, node.local_block_broadcaster.contains (sends[1]->hash ()));
	ASSERT_TIMELY (5s, count (node, nano::stat::detail::broadcast) >= 1);

	// Both get confirmed behind the broadcaster's back, the second one before it was ever flooded
	nano::test::confirm (node.ledger, sends[1]);
	ASSERT_TRUE (node.local_block_broadcaster.contains (sends[1]->hash ()));

	// The second block still gets its first flood, only then does cleanup let go of it
	ASSERT_TIMELY (5s, count (node, nano::stat::detail::broadcast) >= 2);
	ASSERT_TIMELY_EQ (5s, node.local_block_broadcaster.size (), 0);
	ASSERT_EQ (count (node, nano::stat::detail::already_confirmed), 2);
}

/*
 * Floods beyond the limiter's burst are spread out at the configured rate
 */
TEST (local_block_broadcaster, paces_floods_by_rate_limit)
{
	nano::test::system system;
	auto config = system.default_config ();
	config.local_block_broadcaster->broadcast_rate_limit = 1; // Three at once, after that one per second
	auto & node = *system.add_node (config, quiet_flags ());

	auto sends = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 1 }, 5);
	auto const start = std::chrono::steady_clock::now ();
	for (auto const & block : sends)
	{
		ASSERT_EQ (nano::block_status::progress, node.process_local (block).value ());
	}

	// The burst goes out at once, the two beyond it need a token each and so at least two seconds since the burst drained the bucket
	ASSERT_TIMELY (5s, count (node, nano::stat::detail::broadcast) >= 3);
	ASSERT_TIMELY (10s, count (node, nano::stat::detail::broadcast) >= 5);
	ASSERT_GE (std::chrono::steady_clock::now () - start, 2s);
	ASSERT_GE (count (node, nano::stat::detail::cooldown), 1);
}

/*
 * A block is rebroadcast after `rebroadcast_interval`, then after twice that
 */
TEST (local_block_broadcaster, rebroadcast_interval_grows)
{
	nano::test::system system;
	auto & node = *system.add_node (quiet_flags ());

	auto send = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 1 }, 1).front ();
	auto const start = std::chrono::steady_clock::now ();
	ASSERT_EQ (nano::block_status::progress, node.process_local (send).value ());

	// The first rebroadcast follows after one second and the second one after two more, so the third flood is at least three seconds out
	ASSERT_TIMELY (10s, count (node, nano::stat::detail::broadcast) >= 3);
	ASSERT_GE (std::chrono::steady_clock::now () - start, 3s);
}

/*
 * The rebroadcast interval stops growing at `max_rebroadcast_interval`
 */
TEST (local_block_broadcaster, rebroadcast_interval_capped)
{
	nano::test::system system;
	auto config = system.default_config ();
	config.local_block_broadcaster->max_rebroadcast_interval = 1s;
	auto & node = *system.add_node (config, quiet_flags ());

	auto send = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 1 }, 1).front ();
	ASSERT_EQ (nano::block_status::progress, node.process_local (send).value ());
	ASSERT_TIMELY_EQ (5s, count (node, nano::stat::detail::broadcast), 1);

	// Capped at one second the fifth flood is four seconds out, with growing intervals it would be ten
	ASSERT_TIMELY (8s, count (node, nano::stat::detail::broadcast) >= 5);
}

/*
 * A disabled broadcaster tracks nothing and floods nothing
 */
TEST (local_block_broadcaster, disabled)
{
	nano::test::system system;
	auto config = system.default_config ();
	config.local_block_broadcaster->enable = false;
	auto & node = *system.add_node (config, quiet_flags ());

	auto send = genesis_sends (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount, nano::account{ 1 }, 1).front ();
	ASSERT_EQ (nano::block_status::progress, node.process_local (send).value ());
	ASSERT_ALWAYS (500ms, node.local_block_broadcaster.size () == 0 && count (node, nano::stat::detail::broadcast) == 0);
	ASSERT_FALSE (node.local_block_broadcaster.contains (send->hash ()));
}

/*
 * A new local block is flooded on the next limiter token even while older local blocks are still waiting for their rebroadcasts,
 * instead of only after everything taken in the same pass has gone out
 */
TEST (local_block_broadcaster, new_block_ahead_of_rebroadcasts)
{
	nano::test::system system;

	// Without elections and bootstrap, the local block broadcaster's floods are the only way node1's blocks reach node2
	auto const flags = quiet_flags ();

	// Six blocks go out at once, after that one every 500 ms
	auto config1 = system.default_config ();
	config1.bootstrap->enable = false;
	config1.local_block_broadcaster->broadcast_rate_limit = 2;
	auto & node1 = *system.add_node (config1, flags);

	// Node2 only receives
	auto config2 = system.default_config ();
	config2.bootstrap->enable = false;
	config2.local_block_broadcaster->enable = false;
	auto & node2 = *system.add_node (config2, flags);

	nano::block_builder builder;

	// Fund a second account on both nodes directly, so that its blocks do not depend on the backlog chain
	nano::keypair key;
	auto fund = builder.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				.link (key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.build ();
	auto open = builder.state ()
				.account (key.pub)
				.previous (0)
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::Knano_ratio)
				.link (fund->hash ())
				.work (*system.work.generate (key.pub))
				.sign (key.prv, key.pub)
				.build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (fund));
	ASSERT_EQ (nano::block_status::progress, node1.process (open));
	ASSERT_EQ (nano::block_status::progress, node2.process (fund));
	ASSERT_EQ (nano::block_status::progress, node2.process (open));

	// A dozen local blocks whose first floods the limiter spreads over three seconds
	auto backlog = genesis_sends (system, fund->hash (), nano::dev::constants.genesis_amount - nano::Knano_ratio, key.pub, 12);
	for (auto const & block : backlog)
	{
		ASSERT_EQ (nano::block_status::progress, node1.process_local (block).value ());
	}
	ASSERT_TIMELY (10s, node2.block (backlog.back ()->hash ()));

	// By now every one of them is due again, so rebroadcasts keep the limiter busy; the first of them has gone out
	ASSERT_TIMELY (5s, count (node1, nano::stat::detail::broadcast) > backlog.size ());

	// A new local block of the funded account goes out on the next token, ahead of the rebroadcasts still waiting
	auto send = builder.state ()
				.account (key.pub)
				.previous (open->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::Knano_ratio - 1)
				.link (nano::dev::genesis_key.pub)
				.work (*system.work.generate (open->hash ()))
				.sign (key.prv, key.pub)
				.build ();
	ASSERT_EQ (nano::block_status::progress, node1.process_local (send).value ());
	ASSERT_TIMELY (3s, node2.block (send->hash ()));

	// Most of the rebroadcasts are still pending, the new block did not wait for them
	ASSERT_LT (count (node1, nano::stat::detail::broadcast), 2 * backlog.size ());
}
