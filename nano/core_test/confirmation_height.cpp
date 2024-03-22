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

TEST (confirmation_height, gap_bootstrap)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system{};
		nano::node_flags node_flags{};
		node_flags.confirmation_height_processor_mode = mode_a;
		auto & node1 = *system.add_node (node_flags);
		nano::keypair destination{};
		nano::block_builder builder;
		auto send1 = builder
					 .state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build ();
		node1.work_generate_blocking (*send1);
		auto send2 = builder
					 .state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send1->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 2 * nano::Gxrb_ratio)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build ();
		node1.work_generate_blocking (*send2);
		auto send3 = builder
					 .state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send2->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 3 * nano::Gxrb_ratio)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build ();
		node1.work_generate_blocking (*send3);
		auto open1 = builder
					 .open ()
					 .source (send1->hash ())
					 .representative (destination.pub)
					 .account (destination.pub)
					 .sign (destination.prv, destination.pub)
					 .work (0)
					 .build ();
		node1.work_generate_blocking (*open1);

		// Receive
		auto receive1 = builder
						.receive ()
						.previous (open1->hash ())
						.source (send2->hash ())
						.sign (destination.prv, destination.pub)
						.work (0)
						.build ();
		node1.work_generate_blocking (*receive1);
		auto receive2 = builder
						.receive ()
						.previous (receive1->hash ())
						.source (send3->hash ())
						.sign (destination.prv, destination.pub)
						.work (0)
						.build ();
		node1.work_generate_blocking (*receive2);

		node1.block_processor.add (send1);
		node1.block_processor.add (send2);
		node1.block_processor.add (send3);
		node1.block_processor.add (receive1);
		ASSERT_TIMELY (5s, node1.block (send3->hash ()) != nullptr);

		add_callback_stats (node1);

		// Receive 2 comes in on the live network, however the chain has not been finished so it gets added to unchecked
		node1.process_active (receive2);
		// Waits for the unchecked_map to process the 4 blocks added to the block_processor, saving them in the unchecked table
		auto check_block_is_listed = [&] (nano::store::transaction const & transaction_a, nano::block_hash const & block_hash_a) {
			return !node1.unchecked.get (block_hash_a).empty ();
		};
		ASSERT_TIMELY (5s, check_block_is_listed (node1.store.tx_begin_read (), receive2->previous ()));

		// Confirmation heights should not be updated
		{
			auto transaction (node1.store.tx_begin_read ());
			auto unchecked_count (node1.unchecked.count ());
			ASSERT_EQ (unchecked_count, 2);

			nano::confirmation_height_info confirmation_height_info;
			ASSERT_FALSE (node1.store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
			ASSERT_EQ (1, confirmation_height_info.height);
			ASSERT_EQ (nano::dev::genesis->hash (), confirmation_height_info.frontier);
		}

		// Now complete the chain where the block comes in on the bootstrap network.
		node1.block_processor.add (open1);

		ASSERT_TIMELY_EQ (5s, node1.unchecked.count (), 0);
		// Confirmation height should be unchanged and unchecked should now be 0
		{
			auto transaction = node1.store.tx_begin_read ();
			nano::confirmation_height_info confirmation_height_info;
			ASSERT_FALSE (node1.store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
			ASSERT_EQ (1, confirmation_height_info.height);
			ASSERT_EQ (nano::dev::genesis->hash (), confirmation_height_info.frontier);
			ASSERT_TRUE (node1.store.confirmation_height.get (transaction, destination.pub, confirmation_height_info));
			ASSERT_EQ (0, confirmation_height_info.height);
			ASSERT_EQ (nano::block_hash (0), confirmation_height_info.frontier);
		}
		ASSERT_EQ (0, node1.stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (0, node1.stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (0, node1.stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out));
		ASSERT_EQ (1, node1.ledger.cache.cemented_count);

		ASSERT_EQ (0, node1.active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

TEST (confirmation_height, gap_live)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system{};
		nano::node_flags node_flags{};
		node_flags.confirmation_height_processor_mode = mode_a;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);
		node_config.peering_port = system.get_available_port ();
		node_config.receive_minimum = nano::dev::constants.genesis_amount; // Prevent auto-receive & open1/receive1/receive2 blocks conflicts
		system.add_node (node_config, node_flags);
		nano::keypair destination;
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		system.wallet (1)->insert_adhoc (destination.prv);

		nano::block_builder builder;
		auto send1 = builder
					 .state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 1)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build ();
		node->work_generate_blocking (*send1);
		auto send2 = builder
					 .state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send1->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 2)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build ();
		node->work_generate_blocking (*send2);
		auto send3 = builder
					 .state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send2->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 3)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build ();
		node->work_generate_blocking (*send3);

		auto open1 = builder
					 .open ()
					 .source (send1->hash ())
					 .representative (destination.pub)
					 .account (destination.pub)
					 .sign (destination.prv, destination.pub)
					 .work (0)
					 .build ();
		node->work_generate_blocking (*open1);
		auto receive1 = builder
						.receive ()
						.previous (open1->hash ())
						.source (send2->hash ())
						.sign (destination.prv, destination.pub)
						.work (0)
						.build ();
		node->work_generate_blocking (*receive1);
		auto receive2 = builder
						.receive ()
						.previous (receive1->hash ())
						.source (send3->hash ())
						.sign (destination.prv, destination.pub)
						.work (0)
						.build ();
		node->work_generate_blocking (*receive2);

		node->block_processor.add (send1);
		node->block_processor.add (send2);
		node->block_processor.add (send3);
		// node->block_processor.add (open1); Witheld for test
		node->block_processor.add (receive1);
		ASSERT_TIMELY (5s, nano::test::exists (*node, { send1, send2, send3 }));
		ASSERT_TIMELY (5s, node->unchecked.exists ({ open1->hash (), receive1->hash () }));

		add_callback_stats (*node);

		// Receive 2 comes in on the live network, however the chain has not been finished so it gets added to unchecked
		node->process_active (receive2);
		ASSERT_TIMELY (5s, node->unchecked.exists ({ receive1->hash (), receive2->hash () }));

		// Confirmation heights should not be updated
		{
			auto transaction = node->store.tx_begin_read ();
			nano::confirmation_height_info confirmation_height_info;
			ASSERT_FALSE (node->store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
			ASSERT_EQ (1, confirmation_height_info.height);
			ASSERT_EQ (nano::dev::genesis->hash (), confirmation_height_info.frontier);
		}

		// Vote and confirm all existing blocks
		nano::test::start_election (system, *node, send1->hash ());
		ASSERT_TIMELY_EQ (10s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out), 3);

		// Now complete the chain where the block comes in on the live network
		node->process_active (open1);

		ASSERT_TIMELY_EQ (10s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out), 6);

		// This should confirm the open block and the source of the receive blocks
		auto transaction = node->store.tx_begin_read ();
		auto unchecked_count = node->unchecked.count ();
		ASSERT_EQ (unchecked_count, 0);

		nano::confirmation_height_info confirmation_height_info{};
		ASSERT_TRUE (node->ledger.block_confirmed (transaction, receive2->hash ()));
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
		ASSERT_EQ (4, confirmation_height_info.height);
		ASSERT_EQ (send3->hash (), confirmation_height_info.frontier);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, destination.pub, confirmation_height_info));
		ASSERT_EQ (3, confirmation_height_info.height);
		ASSERT_EQ (receive2->hash (), confirmation_height_info.frontier);

		ASSERT_EQ (6, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (6, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (6, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out));
		ASSERT_EQ (7, node->ledger.cache.cemented_count);

		ASSERT_EQ (0, node->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
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

// This test checks that a receive block with uncemented blocks below cements them too.
TEST (confirmation_height, cemented_gap_below_receive)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);

		nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

		nano::keypair key1;
		nano::block_builder builder;
		system.wallet (0)->insert_adhoc (key1.prv);

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
		nano::keypair dummy_key;
		auto dummy_send = builder
						  .send ()
						  .previous (send1->hash ())
						  .destination (dummy_key.pub)
						  .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 3)
						  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						  .work (*system.work.generate (send1->hash ()))
						  .build ();

		auto open = builder
					.open ()
					.source (send->hash ())
					.representative (nano::dev::genesis_key.pub)
					.account (key1.pub)
					.sign (key1.prv, key1.pub)
					.work (*system.work.generate (key1.pub))
					.build ();
		auto receive1 = builder
						.receive ()
						.previous (open->hash ())
						.source (send1->hash ())
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (open->hash ()))
						.build ();
		auto send2 = builder
					 .send ()
					 .previous (receive1->hash ())
					 .destination (nano::dev::genesis_key.pub)
					 .balance (nano::Gxrb_ratio)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (receive1->hash ()))
					 .build ();

		auto receive2 = builder
						.receive ()
						.previous (dummy_send->hash ())
						.source (send2->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (dummy_send->hash ()))
						.build ();
		auto dummy_send1 = builder
						   .send ()
						   .previous (receive2->hash ())
						   .destination (dummy_key.pub)
						   .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 3)
						   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						   .work (*system.work.generate (receive2->hash ()))
						   .build ();

		nano::keypair key2;
		system.wallet (0)->insert_adhoc (key2.prv);
		auto send3 = builder
					 .send ()
					 .previous (dummy_send1->hash ())
					 .destination (key2.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 4)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (dummy_send1->hash ()))
					 .build ();
		auto dummy_send2 = builder
						   .send ()
						   .previous (send3->hash ())
						   .destination (dummy_key.pub)
						   .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 5)
						   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						   .work (*system.work.generate (send3->hash ()))
						   .build ();

		auto open1 = builder
					 .open ()
					 .source (send3->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .account (key2.pub)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();

		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send1));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, dummy_send));

			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, open));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, receive1));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send2));

			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, receive2));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, dummy_send1));

			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send3));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, dummy_send2));

			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, open1));
		}

		std::vector<nano::block_hash> observer_order;
		nano::mutex mutex;
		add_callback_stats (*node, &observer_order, &mutex);

		auto election = nano::test::start_election (system, *node, open1->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();
		ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out), 10);

		auto transaction = node->store.tx_begin_read ();
		ASSERT_TRUE (node->ledger.block_confirmed (transaction, open1->hash ()));
		ASSERT_EQ (1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out));
		ASSERT_EQ (0, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_conf_height, nano::stat::dir::out));
		ASSERT_EQ (9, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
		ASSERT_EQ (10, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (10, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (11, node->ledger.cache.cemented_count);
		ASSERT_EQ (0, node->active.election_winner_details_size ());

		// Check that the order of callbacks is correct
		std::vector<nano::block_hash> expected_order = { send->hash (), open->hash (), send1->hash (), receive1->hash (), send2->hash (), dummy_send->hash (), receive2->hash (), dummy_send1->hash (), send3->hash (), open1->hash () };
		nano::lock_guard<nano::mutex> guard (mutex);
		ASSERT_EQ (observer_order, expected_order);
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

// This test checks that a receive block with uncemented blocks below cements them too, compared with the test above, this
// is the first write in this chain.
TEST (confirmation_height, cemented_gap_below_no_cache)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);

		nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

		nano::keypair key1;
		system.wallet (0)->insert_adhoc (key1.prv);

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
		nano::keypair dummy_key;
		auto dummy_send = builder
						  .send ()
						  .previous (send1->hash ())
						  .destination (dummy_key.pub)
						  .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 3)
						  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						  .work (*system.work.generate (send1->hash ()))
						  .build ();

		auto open = builder
					.open ()
					.source (send->hash ())
					.representative (nano::dev::genesis_key.pub)
					.account (key1.pub)
					.sign (key1.prv, key1.pub)
					.work (*system.work.generate (key1.pub))
					.build ();
		auto receive1 = builder
						.receive ()
						.previous (open->hash ())
						.source (send1->hash ())
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (open->hash ()))
						.build ();
		auto send2 = builder
					 .send ()
					 .previous (receive1->hash ())
					 .destination (nano::dev::genesis_key.pub)
					 .balance (nano::Gxrb_ratio)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (receive1->hash ()))
					 .build ();

		auto receive2 = builder
						.receive ()
						.previous (dummy_send->hash ())
						.source (send2->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (dummy_send->hash ()))
						.build ();
		auto dummy_send1 = builder
						   .send ()
						   .previous (receive2->hash ())
						   .destination (dummy_key.pub)
						   .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 3)
						   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						   .work (*system.work.generate (receive2->hash ()))
						   .build ();

		nano::keypair key2;
		system.wallet (0)->insert_adhoc (key2.prv);
		auto send3 = builder
					 .send ()
					 .previous (dummy_send1->hash ())
					 .destination (key2.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 4)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (dummy_send1->hash ()))
					 .build ();
		auto dummy_send2 = builder
						   .send ()
						   .previous (send3->hash ())
						   .destination (dummy_key.pub)
						   .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 5)
						   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						   .work (*system.work.generate (send3->hash ()))
						   .build ();

		auto open1 = builder
					 .open ()
					 .source (send3->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .account (key2.pub)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();

		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send1));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, dummy_send));

			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, open));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, receive1));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send2));

			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, receive2));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, dummy_send1));

			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, send3));
			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, dummy_send2));

			ASSERT_EQ (nano::block_status::progress, node->ledger.process (transaction, open1));
		}

		// Force some blocks to be cemented so that the cached confirmed info variable is empty
		{
			auto transaction (node->store.tx_begin_write ());
			node->store.confirmation_height.put (transaction, nano::dev::genesis_key.pub, nano::confirmation_height_info{ 3, send1->hash () });
			node->store.confirmation_height.put (transaction, key1.pub, nano::confirmation_height_info{ 2, receive1->hash () });
		}

		add_callback_stats (*node);

		auto election = nano::test::start_election (system, *node, open1->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();
		ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out), 6);

		auto transaction = node->store.tx_begin_read ();
		ASSERT_TRUE (node->ledger.block_confirmed (transaction, open1->hash ()));
		ASSERT_EQ (node->active.election_winner_details_size (), 0);
		ASSERT_EQ (1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out));
		ASSERT_EQ (0, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_conf_height, nano::stat::dir::out));
		ASSERT_EQ (5, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
		ASSERT_EQ (6, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (6, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (7, node->ledger.cache.cemented_count);
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}
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

TEST (confirmation_height, unbounded_block_cache_iteration)
{
	if (nano::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	nano::logger logger;
	auto path (nano::unique_path ());
	auto store = nano::make_store (logger, path, nano::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	nano::stats stats;
	nano::ledger ledger (*store, stats, nano::dev::constants);
	nano::write_database_queue write_database_queue (false);
	boost::latch initialized_latch{ 0 };
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::keypair key1;
	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (nano::dev::genesis->hash ())
				.destination (key1.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*pool.generate (nano::dev::genesis->hash ()))
				.build ();
	auto send1 = builder
				 .send ()
				 .previous (send->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send->hash ()))
				 .build ();
	{
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, ledger.cache, nano::dev::constants);
		ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send));
		ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send1));
	}

	nano::confirmation_height_processor confirmation_height_processor (ledger, write_database_queue, 10ms, logger, initialized_latch, nano::confirmation_height_mode::unbounded);
	nano::timer<> timer;
	timer.start ();
	{
		// Prevent conf height processor doing any writes, so that we can query is_processing_block correctly
		auto write_guard = write_database_queue.wait (nano::writer::testing);
		// Add the frontier block
		confirmation_height_processor.add (send1);

		// The most uncemented block (previous block) should be seen as processing by the unbounded processor
		while (!confirmation_height_processor.exists (send->hash ()))
		{
			ASSERT_LT (timer.since_start (), 10s);
		}
	}

	// Wait until the current block is finished processing
	while (!confirmation_height_processor.current ().is_zero ())
	{
		ASSERT_LT (timer.since_start (), 10s);
	}

	ASSERT_EQ (2, stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
	ASSERT_EQ (2, stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed_unbounded, nano::stat::dir::in));
	ASSERT_EQ (3, ledger.cache.cemented_count);
}
