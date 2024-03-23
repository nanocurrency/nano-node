#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/confirmation_height_processor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace
{
void add_callback_stats (nano::node & node, std::vector<nano::block_hash> * observer_order = nullptr, nano::mutex * mutex = nullptr)
{
	node.observers.blocks.add ([&stats = node.stats, observer_order, mutex] (nano::election_status const & status_a, std::vector<nano::vote_with_weight_info> const &, nano::account const &, nano::amount const &, bool, bool) {
		stats.inc (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out);
		if (mutex)
		{
			nano::lock_guard<nano::mutex> guard (*mutex);
			debug_assert (observer_order);
			observer_order->push_back (status_a.winner->hash ());
		}
	});
}
nano::stat::detail get_stats_detail (nano::confirmation_height_mode mode_a)
{
	debug_assert (mode_a == nano::confirmation_height_mode::bounded || mode_a == nano::confirmation_height_mode::unbounded);
	return (mode_a == nano::confirmation_height_mode::bounded) ? nano::stat::detail::blocks_confirmed_bounded : nano::stat::detail::blocks_confirmed_unbounded;
}
}

TEST (confirmation_height, pending_observer_callbacks)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);

		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

		nano::keypair key1;
		nano::block_builder builder;
		auto send = builder
					.send ()
					.previous (latest)
					.destination (key1.pub)
					.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();
		auto send1 = builder
					 .send ()
					 .previous (send->hash ())
					 .destination (key1.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send->hash ()))
					 .build ();

		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send1));
		}

		add_callback_stats (*node);

		node->confirmation_height_processor.add (send1);

		// Callback is performed for all blocks that are confirmed
		ASSERT_TIMELY_EQ (5s, 2, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out))
		ASSERT_TIMELY_EQ (5s, 2, node->ledger.stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::all, nano::stat::dir::out));

		ASSERT_EQ (2, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (2, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (3, node->ledger.cache.cemented_count);
		ASSERT_EQ (0, node->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

// The callback and confirmation history should only be updated after confirmation height is set (and not just after voting)
TEST (confirmation_height, callback_confirmed_history)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.force_use_write_database_queue = true;
		node_flags.confirmation_height_processor_mode = mode_a;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);

		nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

		nano::keypair key1;
		nano::block_builder builder;
		auto send = builder
					.send ()
					.previous (latest)
					.destination (key1.pub)
					.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send));
		}

		auto send1 = builder
					 .send ()
					 .previous (send->hash ())
					 .destination (key1.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send->hash ()))
					 .build ();

		add_callback_stats (*node);

		node->process_active (send1);
		std::shared_ptr<nano::election> election;
		ASSERT_TIMELY (5s, election = nano::test::start_election (system, *node, send1->hash ()));
		{
			// The write guard prevents the confirmation height processor doing any writes
			auto write_guard = node->write_database_queue.wait (nano::writer::testing);

			// Confirm send1
			election->force_confirm ();
			ASSERT_TIMELY_EQ (10s, node->active.size (), 0);
			ASSERT_EQ (0, node->active.recently_cemented.list ().size ());
			ASSERT_TRUE (node->active.empty ());

			auto transaction = node->store.tx_begin_read ();
			ASSERT_FALSE (node->ledger.block_confirmed (transaction, send->hash ()));

			ASSERT_TIMELY (10s, node->write_database_queue.contains (nano::writer::confirmation_height));

			// Confirm that no inactive callbacks have been called when the confirmation height processor has already iterated over it, waiting to write
			ASSERT_EQ (0, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
		}

		ASSERT_TIMELY (10s, !node->write_database_queue.contains (nano::writer::confirmation_height));

		auto transaction = node->store.tx_begin_read ();
		ASSERT_TRUE (node->ledger.block_confirmed (transaction, send->hash ()));

		ASSERT_TIMELY_EQ (10s, node->active.size (), 0);
		ASSERT_TIMELY_EQ (10s, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out), 1);

		// Each block that's confirmed is in the recently_cemented history
		ASSERT_EQ (2, node->active.recently_cemented.list ().size ());
		ASSERT_TRUE (node->active.empty ());

		// Confirm the callback is not called under this circumstance
		ASSERT_EQ (2, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out));
		ASSERT_EQ (1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out));
		ASSERT_EQ (1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
		ASSERT_EQ (2, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (2, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (3, node->ledger.cache.cemented_count);
		ASSERT_EQ (0, node->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

namespace nano
{
TEST (confirmation_height, dependent_election)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		node_flags.force_use_write_database_queue = true;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);

		nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

		nano::keypair key1;
		nano::block_builder builder;
		auto send = builder
					.send ()
					.previous (latest)
					.destination (key1.pub)
					.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();
		auto send1 = builder
					 .send ()
					 .previous (send->hash ())
					 .destination (key1.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send->hash ()))
					 .build ();
		auto send2 = builder
					 .send ()
					 .previous (send1->hash ())
					 .destination (key1.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 3)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send1->hash ()))
					 .build ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send1));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send2));
		}

		add_callback_stats (*node);

		// This election should be confirmed as active_conf_height
		ASSERT_TRUE (nano::test::start_election (system, *node, send1->hash ()));
		// Start an election and confirm it
		auto election = nano::test::start_election (system, *node, send2->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();

		ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out), 3);

		ASSERT_EQ (1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out));
		ASSERT_EQ (1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_conf_height, nano::stat::dir::out));
		ASSERT_EQ (1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
		ASSERT_EQ (3, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (3, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (4, node->ledger.cache.cemented_count);

		ASSERT_EQ (0, node->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

TEST (confirmation_height, election_winner_details_clearing_node_process_confirmed)
{
	// Make sure election_winner_details is also cleared if the block never enters the confirmation height processor from node::process_confirmed
	nano::test::system system (1);
	auto node = system.nodes.front ();

	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (nano::dev::genesis->hash ())
				.destination (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	// Add to election_winner_details. Use an unrealistic iteration so that it should fall into the else case and do a cleanup
	node->active.add_election_winner_details (send->hash (), nullptr);
	nano::election_status election;
	election.winner = send;
	node->process_confirmed (election, 1000000);
	ASSERT_EQ (0, node->active.election_winner_details_size ());
}
}
