#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/make_store.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/test_common/ledger_context.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <latch>

using namespace std::chrono_literals;

namespace
{
struct confirming_set_context
{
	nano::logger & logger;
	nano::stats & stats;
	nano::ledger & ledger;

	nano::unchecked_map unchecked;
	nano::block_processor block_processor;
	nano::confirming_set confirming_set;

	explicit confirming_set_context (nano::test::ledger_context & ledger_context, nano::node_config node_config = {}) :
		logger{ ledger_context.logger () },
		stats{ ledger_context.stats () },
		ledger{ ledger_context.ledger () },
		unchecked{ 0, stats, false },
		block_processor{ node_config, ledger, unchecked, stats, logger },
		confirming_set{ node_config.confirming_set, ledger, block_processor, stats, logger }
	{
	}
};
}

TEST (confirming_set, construction)
{
	auto ledger_ctx = nano::test::ledger_empty ();
	confirming_set_context ctx{ ledger_ctx };
}

TEST (confirming_set, add_exists)
{
	auto ledger_ctx = nano::test::ledger_send_receive ();
	confirming_set_context ctx{ ledger_ctx };
	nano::confirming_set & confirming_set = ctx.confirming_set;
	auto send = ledger_ctx.blocks ()[0];
	confirming_set.add (send->hash ());
	ASSERT_TRUE (confirming_set.contains (send->hash ()));
}

TEST (confirming_set, process_one)
{
	auto ledger_ctx = nano::test::ledger_send_receive ();
	confirming_set_context ctx{ ledger_ctx };
	nano::confirming_set & confirming_set = ctx.confirming_set;
	std::atomic<int> count = 0;
	std::mutex mutex;
	std::condition_variable condition;
	confirming_set.cemented_observers.add ([&] (auto const &) { ++count; condition.notify_all (); });
	confirming_set.add (ledger_ctx.blocks ()[0]->hash ());
	nano::test::start_stop_guard guard{ confirming_set };
	std::unique_lock lock{ mutex };
	ASSERT_TRUE (condition.wait_for (lock, 5s, [&] () { return count == 1; }));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
	ASSERT_EQ (2, ctx.ledger.cemented_count ());
}

TEST (confirming_set, process_multiple)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto ctx = nano::test::ledger_send_receive ();
	nano::confirming_set_config config{};
	nano::confirming_set confirming_set{ config, ctx.ledger (), node.block_processor, ctx.stats (), ctx.logger () };
	std::atomic<int> count = 0;
	std::mutex mutex;
	std::condition_variable condition;
	confirming_set.cemented_observers.add ([&] (auto const &) { ++count; condition.notify_all (); });
	confirming_set.add (ctx.blocks ()[0]->hash ());
	confirming_set.add (ctx.blocks ()[1]->hash ());
	nano::test::start_stop_guard guard{ confirming_set };
	std::unique_lock lock{ mutex };
	ASSERT_TRUE (condition.wait_for (lock, 5s, [&] () { return count == 2; }));
	ASSERT_EQ (2, ctx.stats ().count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
	ASSERT_EQ (3, ctx.ledger ().cemented_count ());
}

TEST (confirmation_callback, observer_callbacks)
{
	nano::test::system system;
	nano::node_flags node_flags;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_population.enable = false;
	auto node = system.add_node (node_config, node_flags);

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

	nano::keypair key1;
	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (latest)
				.destination (key1.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();
	auto send1 = builder
				 .send ()
				 .previous (send->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio * 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();

	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send1));
	}

	node->confirming_set.add (send1->hash ());

	// Callback is performed for all blocks that are confirmed
	ASSERT_TIMELY_EQ (5s, 2, node->ledger.stats.count (nano::stat::type::confirmation_observer, nano::stat::dir::out));

	ASSERT_EQ (2, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
	ASSERT_EQ (3, node->ledger.cemented_count ());
}

// The callback and confirmation history should only be updated after confirmation height is set (and not just after voting)
TEST (confirmation_callback, confirmed_history)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_ascending_bootstrap = true;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_population.enable = false;
	auto node = system.add_node (node_config, node_flags);

	nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

	nano::keypair key1;
	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (latest)
				.destination (key1.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node->ledger.process (node->ledger.tx_begin_write (), send));

	auto send1 = builder
				 .send ()
				 .previous (send->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio * 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node->ledger.process (node->ledger.tx_begin_write (), send1));

	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = nano::test::start_election (system, *node, send1->hash ()));
	{
		// The write guard prevents the confirmation height processor doing any writes
		auto write_guard = node->store.write_queue.wait (nano::store::writer::testing);

		// Confirm send1
		election->force_confirm ();
		ASSERT_TIMELY_EQ (10s, node->active.size (), 0);
		ASSERT_EQ (0, node->active.recently_cemented.list ().size ());
		ASSERT_TRUE (node->active.empty ());

		auto transaction = node->ledger.tx_begin_read ();
		ASSERT_FALSE (node->ledger.confirmed.block_exists (transaction, send->hash ()));

		ASSERT_TIMELY (10s, node->store.write_queue.contains (nano::store::writer::confirmation_height));

		// Confirm that no inactive callbacks have been called when the confirmation height processor has already iterated over it, waiting to write
		ASSERT_ALWAYS_EQ (50ms, 0, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
	}

	ASSERT_TIMELY (10s, !node->store.write_queue.contains (nano::store::writer::confirmation_height));

	ASSERT_TIMELY (5s, node->ledger.confirmed.block_exists (node->ledger.tx_begin_read (), send->hash ()));

	ASSERT_TIMELY_EQ (10s, node->active.size (), 0);
	ASSERT_TIMELY_EQ (10s, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out), 1);

	// Each block that's confirmed is in the recently_cemented history
	ASSERT_EQ (2, node->active.recently_cemented.list ().size ());
	ASSERT_TRUE (node->active.empty ());

	// Confirm the callback is not called under this circumstance
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 2, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
	ASSERT_EQ (3, node->ledger.cemented_count ());
}

TEST (confirmation_callback, dependent_election)
{
	nano::test::system system;
	nano::node_flags node_flags;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_population.enable = false;
	auto node = system.add_node (node_config, node_flags);

	nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

	nano::keypair key1;
	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (latest)
				.destination (key1.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();
	auto send1 = builder
				 .send ()
				 .previous (send->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio * 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio * 3)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send1));
		ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send2));
	}

	// This election should be confirmed as active_conf_height
	ASSERT_TRUE (nano::test::start_election (system, *node, send1->hash ()));
	// Start an election and confirm it
	auto election = nano::test::start_election (system, *node, send2->hash ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();

	// Wait for blocks to be confirmed in ledger, callbacks will happen after
	ASSERT_TIMELY_EQ (5s, 3, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
	// Once the item added to the confirming set no longer exists, callbacks have completed
	ASSERT_TIMELY (5s, !node->confirming_set.contains (send2->hash ()));

	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_conf_height, nano::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
	ASSERT_EQ (4, node->ledger.cemented_count ());
}
