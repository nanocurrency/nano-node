#include <nano/node/election.hpp>
#include <nano/store/make_store.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/format.hpp>

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

TEST (confirmation_height, single)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		auto amount (std::numeric_limits<nano::uint128_t>::max ());
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		auto node = system.add_node (node_flags);
		nano::keypair key1;
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		nano::block_hash latest1 (node->latest (nano::dev::genesis_key.pub));
		nano::block_builder builder;
		auto send1 = builder
					 .state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (latest1)
					 .representative (nano::dev::genesis_key.pub)
					 .balance (amount - 100)
					 .link (key1.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (latest1))
					 .build_shared ();

		// Check confirmation heights before, should be uninitialized (1 for genesis).
		nano::confirmation_height_info confirmation_height_info;
		add_callback_stats (*node);
		auto transaction = node->store.tx_begin_read ();
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
		ASSERT_EQ (1, confirmation_height_info.height);
		ASSERT_EQ (nano::dev::genesis->hash (), confirmation_height_info.frontier);

		node->process_active (send1);
		node->block_processor.flush ();

		ASSERT_TIMELY (10s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 1);

		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_TRUE (node->ledger.block_confirmed (transaction, send1->hash ()));
			ASSERT_FALSE (node->store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
			ASSERT_EQ (2, confirmation_height_info.height);
			ASSERT_EQ (send1->hash (), confirmation_height_info.frontier);

			// Rollbacks should fail as these blocks have been cemented
			ASSERT_TRUE (node->ledger.rollback (transaction, latest1));
			ASSERT_TRUE (node->ledger.rollback (transaction, send1->hash ()));
			ASSERT_EQ (1, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
			ASSERT_EQ (1, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
			ASSERT_EQ (1, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out));
			ASSERT_EQ (2, node->ledger.cache.cemented_count);

			ASSERT_EQ (0, node->active.election_winner_details_size ());
		}
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

TEST (confirmation_height, multiple_accounts)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);
		nano::keypair key1;
		nano::keypair key2;
		nano::keypair key3;
		nano::block_hash latest1 (system.nodes[0]->latest (nano::dev::genesis_key.pub));
		nano::block_builder builder;

		// Send to all accounts
		auto send1 = builder
					 .send ()
					 .previous (latest1)
					 .destination (key1.pub)
					 .balance (node->online_reps.delta () + 300)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (latest1))
					 .build ();
		auto send2 = builder
					 .send ()
					 .previous (send1->hash ())
					 .destination (key2.pub)
					 .balance (node->online_reps.delta () + 200)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send1->hash ()))
					 .build ();
		auto send3 = builder
					 .send ()
					 .previous (send2->hash ())
					 .destination (key3.pub)
					 .balance (node->online_reps.delta () + 100)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send2->hash ()))
					 .build ();

		// Open all accounts
		auto open1 = builder
					 .open ()
					 .source (send1->hash ())
					 .representative (nano::dev::genesis->account ())
					 .account (key1.pub)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();
		auto open2 = builder
					 .open ()
					 .source (send2->hash ())
					 .representative (nano::dev::genesis->account ())
					 .account (key2.pub)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();
		auto open3 = builder
					 .open ()
					 .source (send3->hash ())
					 .representative (nano::dev::genesis->account ())
					 .account (key3.pub)
					 .sign (key3.prv, key3.pub)
					 .work (*system.work.generate (key3.pub))
					 .build ();

		// Send and receive various blocks to these accounts
		auto send4 = builder
					 .send ()
					 .previous (open1->hash ())
					 .destination (key2.pub)
					 .balance (50)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (open1->hash ()))
					 .build ();
		auto send5 = builder
					 .send ()
					 .previous (send4->hash ())
					 .destination (key2.pub)
					 .balance (10)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (send4->hash ()))
					 .build ();

		auto receive1 = builder
						.receive ()
						.previous (open2->hash ())
						.source (send4->hash ())
						.sign (key2.prv, key2.pub)
						.work (*system.work.generate (open2->hash ()))
						.build ();
		auto send6 = builder
					 .send ()
					 .previous (receive1->hash ())
					 .destination (key3.pub)
					 .balance (10)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (receive1->hash ()))
					 .build ();
		auto receive2 = builder
						.receive ()
						.previous (send6->hash ())
						.source (send5->hash ())
						.sign (key2.prv, key2.pub)
						.work (*system.work.generate (send6->hash ()))
						.build ();

		add_callback_stats (*node);

		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send3).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open3).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send4).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send5).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send6).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive2).code);

			// Check confirmation heights of all the accounts (except genesis) are uninitialized (0),
			// as we have any just added them to the ledger and not processed any live transactions yet.
			nano::confirmation_height_info confirmation_height_info;
			ASSERT_FALSE (node->store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
			ASSERT_EQ (1, confirmation_height_info.height);
			ASSERT_EQ (nano::dev::genesis->hash (), confirmation_height_info.frontier);
			ASSERT_TRUE (node->store.confirmation_height.get (transaction, key1.pub, confirmation_height_info));
			ASSERT_EQ (0, confirmation_height_info.height);
			ASSERT_EQ (nano::block_hash (0), confirmation_height_info.frontier);
			ASSERT_TRUE (node->store.confirmation_height.get (transaction, key2.pub, confirmation_height_info));
			ASSERT_EQ (0, confirmation_height_info.height);
			ASSERT_EQ (nano::block_hash (0), confirmation_height_info.frontier);
			ASSERT_TRUE (node->store.confirmation_height.get (transaction, key3.pub, confirmation_height_info));
			ASSERT_EQ (0, confirmation_height_info.height);
			ASSERT_EQ (nano::block_hash (0), confirmation_height_info.frontier);
		}

		// The nodes process a live receive which propagates across to all accounts
		auto receive3 = builder
						.receive ()
						.previous (open3->hash ())
						.source (send6->hash ())
						.sign (key3.prv, key3.pub)
						.work (*system.work.generate (open3->hash ()))
						.build_shared ();
		node->process_active (receive3);
		auto election = nano::test::start_election (system, *node, receive3->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();

		ASSERT_TIMELY (10s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 10);

		nano::confirmation_height_info confirmation_height_info;
		auto & store = node->store;
		auto transaction = node->store.tx_begin_read ();
		ASSERT_TRUE (node->ledger.block_confirmed (transaction, receive3->hash ()));
		auto account_info = node->ledger.account_info (transaction, nano::dev::genesis_key.pub);
		ASSERT_TRUE (account_info);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
		ASSERT_EQ (4, confirmation_height_info.height);
		ASSERT_EQ (send3->hash (), confirmation_height_info.frontier);
		ASSERT_EQ (4, account_info->block_count);
		account_info = node->ledger.account_info (transaction, key1.pub);
		ASSERT_TRUE (account_info);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, key1.pub, confirmation_height_info));
		ASSERT_EQ (2, confirmation_height_info.height);
		ASSERT_EQ (send4->hash (), confirmation_height_info.frontier);
		ASSERT_EQ (3, account_info->block_count);
		account_info = node->ledger.account_info (transaction, key2.pub);
		ASSERT_TRUE (account_info);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, key2.pub, confirmation_height_info));
		ASSERT_EQ (3, confirmation_height_info.height);
		ASSERT_EQ (send6->hash (), confirmation_height_info.frontier);
		ASSERT_EQ (4, account_info->block_count);
		account_info = node->ledger.account_info (transaction, key3.pub);
		ASSERT_TRUE (account_info);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, key3.pub, confirmation_height_info));
		ASSERT_EQ (2, confirmation_height_info.height);
		ASSERT_EQ (receive3->hash (), confirmation_height_info.frontier);
		ASSERT_EQ (2, account_info->block_count);

		// The accounts for key1 and key2 have 1 more block in the chain than is confirmed.
		// So this can be rolled back, but the one before that cannot. Check that this is the case
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_FALSE (node->ledger.rollback (transaction, node->latest (key2.pub)));
			ASSERT_FALSE (node->ledger.rollback (transaction, node->latest (key1.pub)));
		}
		{
			// These rollbacks should fail
			auto transaction = node->store.tx_begin_write ();
			ASSERT_TRUE (node->ledger.rollback (transaction, node->latest (key1.pub)));
			ASSERT_TRUE (node->ledger.rollback (transaction, node->latest (key2.pub)));

			// Confirm the other latest can't be rolled back either
			ASSERT_TRUE (node->ledger.rollback (transaction, node->latest (key3.pub)));
			ASSERT_TRUE (node->ledger.rollback (transaction, node->latest (nano::dev::genesis_key.pub)));

			// Attempt some others which have been cemented
			ASSERT_TRUE (node->ledger.rollback (transaction, open1->hash ()));
			ASSERT_TRUE (node->ledger.rollback (transaction, send2->hash ()));
		}
		ASSERT_EQ (10, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (10, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (10, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out));
		ASSERT_EQ (11, node->ledger.cache.cemented_count);

		ASSERT_EQ (0, node->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
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
					 .account (nano::dev::genesis->account ())
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis->account ())
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build_shared ();
		node1.work_generate_blocking (*send1);
		auto send2 = builder
					 .state ()
					 .account (nano::dev::genesis->account ())
					 .previous (send1->hash ())
					 .representative (nano::dev::genesis->account ())
					 .balance (nano::dev::constants.genesis_amount - 2 * nano::Gxrb_ratio)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build_shared ();
		node1.work_generate_blocking (*send2);
		auto send3 = builder
					 .state ()
					 .account (nano::dev::genesis->account ())
					 .previous (send2->hash ())
					 .representative (nano::dev::genesis->account ())
					 .balance (nano::dev::constants.genesis_amount - 3 * nano::Gxrb_ratio)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build_shared ();
		node1.work_generate_blocking (*send3);
		auto open1 = builder
					 .open ()
					 .source (send1->hash ())
					 .representative (destination.pub)
					 .account (destination.pub)
					 .sign (destination.prv, destination.pub)
					 .work (0)
					 .build_shared ();
		node1.work_generate_blocking (*open1);

		// Receive
		auto receive1 = builder
						.receive ()
						.previous (open1->hash ())
						.source (send2->hash ())
						.sign (destination.prv, destination.pub)
						.work (0)
						.build_shared ();
		node1.work_generate_blocking (*receive1);
		auto receive2 = builder
						.receive ()
						.previous (receive1->hash ())
						.source (send3->hash ())
						.sign (destination.prv, destination.pub)
						.work (0)
						.build_shared ();
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

		ASSERT_TIMELY (5s, node1.unchecked.count () == 0);
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
					 .account (nano::dev::genesis->account ())
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis->account ())
					 .balance (nano::dev::constants.genesis_amount - 1)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build_shared ();
		node->work_generate_blocking (*send1);
		auto send2 = builder
					 .state ()
					 .account (nano::dev::genesis->account ())
					 .previous (send1->hash ())
					 .representative (nano::dev::genesis->account ())
					 .balance (nano::dev::constants.genesis_amount - 2)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build_shared ();
		node->work_generate_blocking (*send2);
		auto send3 = builder
					 .state ()
					 .account (nano::dev::genesis->account ())
					 .previous (send2->hash ())
					 .representative (nano::dev::genesis->account ())
					 .balance (nano::dev::constants.genesis_amount - 3)
					 .link (destination.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (0)
					 .build_shared ();
		node->work_generate_blocking (*send3);

		auto open1 = builder
					 .open ()
					 .source (send1->hash ())
					 .representative (destination.pub)
					 .account (destination.pub)
					 .sign (destination.prv, destination.pub)
					 .work (0)
					 .build_shared ();
		node->work_generate_blocking (*open1);
		auto receive1 = builder
						.receive ()
						.previous (open1->hash ())
						.source (send2->hash ())
						.sign (destination.prv, destination.pub)
						.work (0)
						.build_shared ();
		node->work_generate_blocking (*receive1);
		auto receive2 = builder
						.receive ()
						.previous (receive1->hash ())
						.source (send3->hash ())
						.sign (destination.prv, destination.pub)
						.work (0)
						.build_shared ();
		node->work_generate_blocking (*receive2);

		node->block_processor.add (send1);
		node->block_processor.add (send2);
		node->block_processor.add (send3);
		node->block_processor.add (receive1);
		node->block_processor.flush ();

		add_callback_stats (*node);

		// Receive 2 comes in on the live network, however the chain has not been finished so it gets added to unchecked
		node->process_active (receive2);
		node->block_processor.flush ();

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
		ASSERT_TIMELY (10s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 3);

		// Now complete the chain where the block comes in on the live network
		node->process_active (open1);
		node->block_processor.flush ();

		ASSERT_TIMELY (10s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 6);

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

TEST (confirmation_height, send_receive_between_2_accounts)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);
		nano::keypair key1;
		nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

		nano::block_builder builder;
		auto send1 = builder
					 .send ()
					 .previous (latest)
					 .destination (key1.pub)
					 .balance (node->online_reps.delta () + 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (latest))
					 .build ();
		auto open1 = builder
					 .open ()
					 .source (send1->hash ())
					 .representative (nano::dev::genesis->account ())
					 .account (key1.pub)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();
		auto send2 = builder
					 .send ()
					 .previous (open1->hash ())
					 .destination (nano::dev::genesis->account ())
					 .balance (1000)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (open1->hash ()))
					 .build ();
		auto send3 = builder
					 .send ()
					 .previous (send2->hash ())
					 .destination (nano::dev::genesis->account ())
					 .balance (900)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (send2->hash ()))
					 .build ();
		auto send4 = builder
					 .send ()
					 .previous (send3->hash ())
					 .destination (nano::dev::genesis->account ())
					 .balance (500)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (send3->hash ()))
					 .build ();
		auto receive1 = builder
						.receive ()
						.previous (send1->hash ())
						.source (send2->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (send1->hash ()))
						.build ();
		auto receive2 = builder
						.receive ()
						.previous (receive1->hash ())
						.source (send3->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (receive1->hash ()))
						.build ();
		auto receive3 = builder
						.receive ()
						.previous (receive2->hash ())
						.source (send4->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (receive2->hash ()))
						.build ();
		auto send5 = builder
					 .send ()
					 .previous (receive3->hash ())
					 .destination (key1.pub)
					 .balance (node->online_reps.delta () + 1)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (receive3->hash ()))
					 .build ();
		auto receive4 = builder
						.receive ()
						.previous (send4->hash ())
						.source (send5->hash ())
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (send4->hash ()))
						.build_shared ();
		nano::keypair key2;
		auto send6 = builder
					 .send ()
					 .previous (send5->hash ())
					 .destination (key2.pub)
					 .balance (node->online_reps.delta () + 1)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send5->hash ()))
					 .build ();
		// Unpocketed send
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open1).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive1).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send3).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send4).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive3).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send5).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send6).code);
		}

		add_callback_stats (*node);

		node->process_active (receive4);
		auto election = nano::test::start_election (system, *node, receive4->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();

		ASSERT_TIMELY (10s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 10);

		auto transaction (node->store.tx_begin_read ());
		ASSERT_TRUE (node->ledger.block_confirmed (transaction, receive4->hash ()));
		nano::confirmation_height_info confirmation_height_info;
		auto account_info = node->ledger.account_info (transaction, nano::dev::genesis_key.pub);
		ASSERT_TRUE (account_info);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
		ASSERT_EQ (6, confirmation_height_info.height);
		ASSERT_EQ (send5->hash (), confirmation_height_info.frontier);
		ASSERT_EQ (7, account_info->block_count);

		account_info = node->ledger.account_info (transaction, key1.pub);
		ASSERT_TRUE (account_info);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, key1.pub, confirmation_height_info));
		ASSERT_EQ (5, confirmation_height_info.height);
		ASSERT_EQ (receive4->hash (), confirmation_height_info.frontier);
		ASSERT_EQ (5, account_info->block_count);

		ASSERT_EQ (10, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (10, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (10, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out));
		ASSERT_EQ (11, node->ledger.cache.cemented_count);

		ASSERT_EQ (0, node->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

TEST (confirmation_height, send_receive_self)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);
		nano::block_hash latest (node->latest (nano::dev::genesis_key.pub));

		nano::block_builder builder;
		auto send1 = builder
					 .send ()
					 .previous (latest)
					 .destination (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (latest))
					 .build ();
		auto receive1 = builder
						.receive ()
						.previous (send1->hash ())
						.source (send1->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (send1->hash ()))
						.build_shared ();
		auto send2 = builder
					 .send ()
					 .previous (receive1->hash ())
					 .destination (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (receive1->hash ()))
					 .build ();
		auto send3 = builder
					 .send ()
					 .previous (send2->hash ())
					 .destination (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - 3)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send2->hash ()))
					 .build ();
		auto receive2 = builder
						.receive ()
						.previous (send3->hash ())
						.source (send2->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (send3->hash ()))
						.build ();
		auto receive3 = builder
						.receive ()
						.previous (receive2->hash ())
						.source (send3->hash ())
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (receive2->hash ()))
						.build_shared ();

		// Send to another account to prevent automatic receiving on the genesis account
		nano::keypair key1;
		auto send4 = builder
					 .send ()
					 .previous (receive3->hash ())
					 .destination (key1.pub)
					 .balance (node->online_reps.delta ())
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (receive3->hash ()))
					 .build ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send3).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive3).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send4).code);
		}

		add_callback_stats (*node);

		auto election = nano::test::start_election (system, *node, receive3->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();

		ASSERT_TIMELY (5s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 6);

		auto transaction (node->store.tx_begin_read ());
		ASSERT_TRUE (node->ledger.block_confirmed (transaction, receive3->hash ()));
		auto account_info = node->ledger.account_info (transaction, nano::dev::genesis_key.pub);
		ASSERT_TRUE (account_info);
		nano::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
		ASSERT_EQ (7, confirmation_height_info.height);
		ASSERT_EQ (receive3->hash (), confirmation_height_info.frontier);
		ASSERT_EQ (8, account_info->block_count);
		ASSERT_EQ (6, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (6, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (6, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out));
		ASSERT_EQ (confirmation_height_info.height, node->ledger.cache.cemented_count);
		ASSERT_EQ (0, node->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

TEST (confirmation_height, all_block_types)
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
		nano::keypair key2;
		auto & store = node->store;
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
					 .destination (key2.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send->hash ()))
					 .build ();

		auto open = builder
					.open ()
					.source (send->hash ())
					.representative (nano::dev::genesis_key.pub)
					.account (key1.pub)
					.sign (key1.prv, key1.pub)
					.work (*system.work.generate (key1.pub))
					.build ();
		auto state_open = builder
						  .state ()
						  .account (key2.pub)
						  .previous (0)
						  .representative (0)
						  .balance (nano::Gxrb_ratio)
						  .link (send1->hash ())
						  .sign (key2.prv, key2.pub)
						  .work (*system.work.generate (key2.pub))
						  .build ();

		auto send2 = builder
					 .send ()
					 .previous (open->hash ())
					 .destination (key2.pub)
					 .balance (0)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (open->hash ()))
					 .build ();
		auto state_receive = builder
							 .state ()
							 .account (key2.pub)
							 .previous (state_open->hash ())
							 .representative (0)
							 .balance (nano::Gxrb_ratio * 2)
							 .link (send2->hash ())
							 .sign (key2.prv, key2.pub)
							 .work (*system.work.generate (state_open->hash ()))
							 .build ();

		auto state_send = builder
						  .state ()
						  .account (key2.pub)
						  .previous (state_receive->hash ())
						  .representative (0)
						  .balance (nano::Gxrb_ratio)
						  .link (key1.pub)
						  .sign (key2.prv, key2.pub)
						  .work (*system.work.generate (state_receive->hash ()))
						  .build ();
		auto receive = builder
					   .receive ()
					   .previous (send2->hash ())
					   .source (state_send->hash ())
					   .sign (key1.prv, key1.pub)
					   .work (*system.work.generate (send2->hash ()))
					   .build ();

		auto change = builder
					  .change ()
					  .previous (receive->hash ())
					  .representative (key2.pub)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (receive->hash ()))
					  .build ();

		auto state_change = builder
							.state ()
							.account (key2.pub)
							.previous (state_send->hash ())
							.representative (nano::dev::genesis_key.pub)
							.balance (nano::Gxrb_ratio)
							.link (0)
							.sign (key2.prv, key2.pub)
							.work (*system.work.generate (state_send->hash ()))
							.build ();

		auto epoch = builder
					 .state ()
					 .account (key2.pub)
					 .previous (state_change->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::Gxrb_ratio)
					 .link (node->ledger.epoch_link (nano::epoch::epoch_1))
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (state_change->hash ()))
					 .build ();

		auto epoch1 = builder
					  .state ()
					  .account (key1.pub)
					  .previous (change->hash ())
					  .representative (key2.pub)
					  .balance (nano::Gxrb_ratio)
					  .link (node->ledger.epoch_link (nano::epoch::epoch_1))
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*system.work.generate (change->hash ()))
					  .build ();
		auto state_send1 = builder
						   .state ()
						   .account (key1.pub)
						   .previous (epoch1->hash ())
						   .representative (0)
						   .balance (nano::Gxrb_ratio - 1)
						   .link (key2.pub)
						   .sign (key1.prv, key1.pub)
						   .work (*system.work.generate (epoch1->hash ()))
						   .build ();
		auto state_receive2 = builder
							  .state ()
							  .account (key2.pub)
							  .previous (epoch->hash ())
							  .representative (0)
							  .balance (nano::Gxrb_ratio + 1)
							  .link (state_send1->hash ())
							  .sign (key2.prv, key2.pub)
							  .work (*system.work.generate (epoch->hash ()))
							  .build ();

		auto state_send2 = builder
						   .state ()
						   .account (key2.pub)
						   .previous (state_receive2->hash ())
						   .representative (0)
						   .balance (nano::Gxrb_ratio)
						   .link (key1.pub)
						   .sign (key2.prv, key2.pub)
						   .work (*system.work.generate (state_receive2->hash ()))
						   .build_shared ();
		auto state_send3 = builder
						   .state ()
						   .account (key2.pub)
						   .previous (state_send2->hash ())
						   .representative (0)
						   .balance (nano::Gxrb_ratio - 1)
						   .link (key1.pub)
						   .sign (key2.prv, key2.pub)
						   .work (*system.work.generate (state_send2->hash ()))
						   .build ();

		auto state_send4 = builder
						   .state ()
						   .account (key1.pub)
						   .previous (state_send1->hash ())
						   .representative (0)
						   .balance (nano::Gxrb_ratio - 2)
						   .link (nano::dev::genesis_key.pub)
						   .sign (key1.prv, key1.pub)
						   .work (*system.work.generate (state_send1->hash ()))
						   .build ();
		auto state_receive3 = builder
							  .state ()
							  .account (nano::dev::genesis->account ())
							  .previous (send1->hash ())
							  .representative (nano::dev::genesis->account ())
							  .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2 + 1)
							  .link (state_send4->hash ())
							  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
							  .work (*system.work.generate (send1->hash ()))
							  .build ();

		{
			auto transaction (store.tx_begin_write ());
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_open).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_receive).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_send).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *change).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_change).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *epoch).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *epoch1).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_send1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_receive2).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_send2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_send3).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_send4).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *state_receive3).code);
		}

		add_callback_stats (*node);
		auto election = nano::test::start_election (system, *node, state_send2->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();

		ASSERT_TIMELY (5s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 15);

		auto transaction (node->store.tx_begin_read ());
		ASSERT_TRUE (node->ledger.block_confirmed (transaction, state_send2->hash ()));
		nano::confirmation_height_info confirmation_height_info;
		auto account_info = node->ledger.account_info (transaction, nano::dev::genesis_key.pub);
		ASSERT_TRUE (account_info);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, nano::dev::genesis_key.pub, confirmation_height_info));
		ASSERT_EQ (3, confirmation_height_info.height);
		ASSERT_EQ (send1->hash (), confirmation_height_info.frontier);
		ASSERT_LE (4, account_info->block_count);

		account_info = node->ledger.account_info (transaction, key1.pub);
		ASSERT_TRUE (account_info);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, key1.pub, confirmation_height_info));
		ASSERT_EQ (state_send1->hash (), confirmation_height_info.frontier);
		ASSERT_EQ (6, confirmation_height_info.height);
		ASSERT_LE (7, account_info->block_count);

		account_info = node->ledger.account_info (transaction, key2.pub);
		ASSERT_TRUE (account_info);
		ASSERT_FALSE (node->store.confirmation_height.get (transaction, key2.pub, confirmation_height_info));
		ASSERT_EQ (7, confirmation_height_info.height);
		ASSERT_EQ (state_send2->hash (), confirmation_height_info.frontier);
		ASSERT_LE (8, account_info->block_count);

		ASSERT_EQ (15, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (15, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (15, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out));
		ASSERT_EQ (16, node->ledger.cache.cemented_count);

		ASSERT_EQ (0, node->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

// This test ensures a block that's cemented cannot be rolled back by the node
// A block is inserted and confirmed then later a different block is force inserted with a rollback attempt
TEST (confirmation_height, conflict_rollback_cemented)
{
	// functor to perform the conflict_rollback_cemented test using a certain mode
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::state_block_builder builder;
		auto const genesis_hash = nano::dev::genesis->hash ();

		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		auto node1 = system.add_node (node_flags);

		nano::keypair key1;
		// create one side of a forked transaction on node1
		auto fork1a = builder.make_block ()
					  .previous (genesis_hash)
					  .account (nano::dev::genesis_key.pub)
					  .representative (nano::dev::genesis_key.pub)
					  .link (key1.pub)
					  .balance (nano::dev::constants.genesis_amount - 100)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*system.work.generate (genesis_hash))
					  .build_shared ();
		ASSERT_EQ (nano::process_result::progress, node1->process (*fork1a).code);
		ASSERT_TRUE (nano::test::start_elections (system, *node1, { fork1a }, true));
		ASSERT_TIMELY (5s, nano::test::confirmed (*node1, { fork1a }));

		// create the other side of the fork on node2
		nano::keypair key2;
		auto fork1b = builder.make_block ()
					  .previous (genesis_hash)
					  .account (nano::dev::genesis_key.pub)
					  .representative (nano::dev::genesis_key.pub)
					  .link (key2.pub) // Different destination same 'previous'
					  .balance (nano::dev::constants.genesis_amount - 100)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*system.work.generate (genesis_hash))
					  .build_shared ();

		node1->block_processor.force (fork1b);
		// node2 already has send2 forced confirmed whilst node1 should have confirmed send1 and therefore we have a cemented fork on node2
		// and node2 should print an error message on the log that it cannot rollback send2 because it is already cemented
		[[maybe_unused]] size_t count = 0;
		ASSERT_TIMELY (5s, 1 == (count = node1->stats.count (nano::stat::type::ledger, nano::stat::detail::rollback_failed)));
		ASSERT_TRUE (nano::test::confirmed (*node1, { fork1a->hash () })); // fork1a should still remain after the rollback failed event
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

TEST (confirmation_heightDeathTest, rollback_added_block)
{
	if (nano::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	// For ASSERT_DEATH_IF_SUPPORTED
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	// valgrind can be noisy with death tests
	if (!nano::running_within_valgrind ())
	{
		nano::logger_mt logger;
		nano::logging logging;
		auto path (nano::unique_path ());
		auto store = nano::make_store (logger, path, nano::dev::constants);
		ASSERT_TRUE (!store->init_error ());
		nano::stats stats;
		nano::ledger ledger (*store, stats, nano::dev::constants);
		nano::write_database_queue write_database_queue (false);
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
					.build_shared ();
		{
			auto transaction (store->tx_begin_write ());
			store->initialize (transaction, ledger.cache, ledger.constants);
		}

		uint64_t batch_write_size = 2048;
		std::atomic<bool> stopped{ false };
		nano::confirmation_height_unbounded unbounded_processor (
		ledger, write_database_queue, 10ms, logging, logger, stopped, batch_write_size, [] (auto const &) {}, [] (auto const &) {}, [] () { return 0; });

		// Processing a block which doesn't exist should bail
		ASSERT_DEATH_IF_SUPPORTED (unbounded_processor.process (send), "");

		nano::confirmation_height_bounded bounded_processor (
		ledger, write_database_queue, 10ms, logging, logger, stopped, batch_write_size, [] (auto const &) {}, [] (auto const &) {}, [] () { return 0; });
		// Processing a block which doesn't exist should bail
		ASSERT_DEATH_IF_SUPPORTED (bounded_processor.process (send), "");
	}
}

TEST (confirmation_height, observers)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		auto amount (std::numeric_limits<nano::uint128_t>::max ());
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.confirmation_height_processor_mode = mode_a;
		auto node1 = system.add_node (node_flags);
		nano::keypair key1;
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		nano::block_hash latest1 (node1->latest (nano::dev::genesis_key.pub));
		nano::block_builder builder;
		auto send1 = builder
					 .send ()
					 .previous (latest1)
					 .destination (key1.pub)
					 .balance (amount - node1->config.receive_minimum.number ())
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (latest1))
					 .build_shared ();

		add_callback_stats (*node1);

		node1->process_active (send1);
		node1->block_processor.flush ();
		ASSERT_TIMELY (10s, node1->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 1);
		auto transaction = node1->store.tx_begin_read ();
		ASSERT_TRUE (node1->ledger.block_confirmed (transaction, send1->hash ()));
		ASSERT_EQ (1, node1->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (1, node1->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (1, node1->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out));
		ASSERT_EQ (2, node1->ledger.cache.cemented_count);
		ASSERT_EQ (0, node1->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}

// This tests when a read has been done, but the block no longer exists by the time a write is done
TEST (confirmation_heightDeathTest, modified_chain)
{
	if (nano::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	// For ASSERT_DEATH_IF_SUPPORTED
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	// valgrind can be noisy with death tests
	if (!nano::running_within_valgrind ())
	{
		nano::logging logging;
		nano::logger_mt logger;
		auto path (nano::unique_path ());
		auto store = nano::make_store (logger, path, nano::dev::constants);
		ASSERT_TRUE (!store->init_error ());
		nano::stats stats;
		nano::ledger ledger (*store, stats, nano::dev::constants);
		nano::write_database_queue write_database_queue (false);
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
					.build_shared ();
		{
			auto transaction (store->tx_begin_write ());
			store->initialize (transaction, ledger.cache, ledger.constants);
			ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *send).code);
		}

		uint64_t batch_write_size = 2048;
		std::atomic<bool> stopped{ false };
		nano::confirmation_height_bounded bounded_processor (
		ledger, write_database_queue, 10ms, logging, logger, stopped, batch_write_size, [] (auto const &) {}, [] (auto const &) {}, [] () { return 0; });

		{
			// This reads the blocks in the account, but prevents any writes from occuring yet
			auto scoped_write_guard = write_database_queue.wait (nano::writer::testing);
			bounded_processor.process (send);
		}

		// Rollback the block and now try to write, the block no longer exists so should bail
		ledger.rollback (store->tx_begin_write (), send->hash ());
		{
			auto scoped_write_guard = write_database_queue.wait (nano::writer::confirmation_height);
			ASSERT_DEATH_IF_SUPPORTED (bounded_processor.cement_blocks (scoped_write_guard), "");
		}

		ASSERT_EQ (nano::process_result::progress, ledger.process (store->tx_begin_write (), *send).code);
		store->confirmation_height.put (store->tx_begin_write (), nano::dev::genesis->account (), { 1, nano::dev::genesis->hash () });

		nano::confirmation_height_unbounded unbounded_processor (
		ledger, write_database_queue, 10ms, logging, logger, stopped, batch_write_size, [] (auto const &) {}, [] (auto const &) {}, [] () { return 0; });

		{
			// This reads the blocks in the account, but prevents any writes from occuring yet
			auto scoped_write_guard = write_database_queue.wait (nano::writer::testing);
			unbounded_processor.process (send);
		}

		// Rollback the block and now try to write, the block no longer exists so should bail
		ledger.rollback (store->tx_begin_write (), send->hash ());
		{
			auto scoped_write_guard = write_database_queue.wait (nano::writer::confirmation_height);
			ASSERT_DEATH_IF_SUPPORTED (unbounded_processor.cement_blocks (scoped_write_guard), "");
		}
	}
}

// This tests when a read has been done, but the account no longer exists by the time a write is done
TEST (confirmation_heightDeathTest, modified_chain_account_removed)
{
	if (nano::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	// For ASSERT_DEATH_IF_SUPPORTED
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	// valgrind can be noisy with death tests
	if (!nano::running_within_valgrind ())
	{
		nano::logging logging;
		nano::logger_mt logger;
		auto path (nano::unique_path ());
		auto store = nano::make_store (logger, path, nano::dev::constants);
		ASSERT_TRUE (!store->init_error ());
		nano::stats stats;
		nano::ledger ledger (*store, stats, nano::dev::constants);
		nano::write_database_queue write_database_queue (false);
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
					.build_shared ();
		auto open = builder
					.state ()
					.account (key1.pub)
					.previous (0)
					.representative (0)
					.balance (nano::Gxrb_ratio)
					.link (send->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (key1.pub))
					.build_shared ();
		{
			auto transaction (store->tx_begin_write ());
			store->initialize (transaction, ledger.cache, ledger.constants);
			ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *send).code);
			ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *open).code);
		}

		uint64_t batch_write_size = 2048;
		std::atomic<bool> stopped{ false };
		nano::confirmation_height_unbounded unbounded_processor (
		ledger, write_database_queue, 10ms, logging, logger, stopped, batch_write_size, [] (auto const &) {}, [] (auto const &) {}, [] () { return 0; });

		{
			// This reads the blocks in the account, but prevents any writes from occuring yet
			auto scoped_write_guard = write_database_queue.wait (nano::writer::testing);
			unbounded_processor.process (open);
		}

		// Rollback the block and now try to write, the send should be cemented but the account which the open block belongs no longer exists so should bail
		ledger.rollback (store->tx_begin_write (), open->hash ());
		{
			auto scoped_write_guard = write_database_queue.wait (nano::writer::confirmation_height);
			ASSERT_DEATH_IF_SUPPORTED (unbounded_processor.cement_blocks (scoped_write_guard), "");
		}

		// Reset conditions and test with the bounded processor
		ASSERT_EQ (nano::process_result::progress, ledger.process (store->tx_begin_write (), *open).code);
		store->confirmation_height.put (store->tx_begin_write (), nano::dev::genesis->account (), { 1, nano::dev::genesis->hash () });

		nano::confirmation_height_bounded bounded_processor (
		ledger, write_database_queue, 10ms, logging, logger, stopped, batch_write_size, [] (auto const &) {}, [] (auto const &) {}, [] () { return 0; });

		{
			// This reads the blocks in the account, but prevents any writes from occuring yet
			auto scoped_write_guard = write_database_queue.wait (nano::writer::testing);
			bounded_processor.process (open);
		}

		// Rollback the block and now try to write, the send should be cemented but the account which the open block belongs no longer exists so should bail
		ledger.rollback (store->tx_begin_write (), open->hash ());
		auto scoped_write_guard = write_database_queue.wait (nano::writer::confirmation_height);
		ASSERT_DEATH_IF_SUPPORTED (bounded_processor.cement_blocks (scoped_write_guard), "");
	}
}

namespace nano
{
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
					 .build_shared ();

		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send1).code);
		}

		add_callback_stats (*node);

		node->confirmation_height_processor.add (send1);

		// Confirm the callback is not called under this circumstance because there is no election information
		ASSERT_TIMELY (10s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 1 && node->ledger.stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::all, nano::stat::dir::out) == 1);

		ASSERT_EQ (2, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		ASSERT_EQ (2, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
		ASSERT_EQ (3, node->ledger.cache.cemented_count);
		ASSERT_EQ (0, node->active.election_winner_details_size ());
	};

	test_mode (nano::confirmation_height_mode::bounded);
	test_mode (nano::confirmation_height_mode::unbounded);
}
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
					.build_shared ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
		}

		auto send1 = builder
					 .send ()
					 .previous (send->hash ())
					 .destination (key1.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send->hash ()))
					 .build_shared ();

		add_callback_stats (*node);

		node->process_active (send1);
		ASSERT_NE (nano::test::start_election (system, *node, send1->hash ()), nullptr);
		{
			node->process_active (send);
			node->block_processor.flush ();

			// The write guard prevents the confirmation height processor doing any writes
			auto write_guard = node->write_database_queue.wait (nano::writer::testing);

			// Confirm send1
			auto election = node->active.election (send1->qualified_root ());
			ASSERT_NE (nullptr, election);
			election->force_confirm ();
			ASSERT_TIMELY (10s, node->active.size () == 0);
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

		ASSERT_TIMELY (10s, node->active.size () == 0);
		ASSERT_TIMELY (10s, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out) == 1);

		ASSERT_EQ (1, node->active.recently_cemented.list ().size ());
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
					.build_shared ();
		auto send1 = builder
					 .send ()
					 .previous (send->hash ())
					 .destination (key1.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send->hash ()))
					 .build_shared ();
		auto send2 = builder
					 .send ()
					 .previous (send1->hash ())
					 .destination (key1.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 3)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send1->hash ()))
					 .build_shared ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send2).code);
		}

		add_callback_stats (*node);

		// This election should be confirmed as active_conf_height
		ASSERT_TRUE (nano::test::start_election (system, *node, send1->hash ()));
		// Start an election and confirm it
		auto election = nano::test::start_election (system, *node, send2->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();

		ASSERT_TIMELY (5s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 3);

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
					.representative (nano::dev::genesis->account ())
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
					 .representative (nano::dev::genesis->account ())
					 .account (key2.pub)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build_shared ();

		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *dummy_send).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send2).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *dummy_send1).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send3).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *dummy_send2).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open1).code);
		}

		std::vector<nano::block_hash> observer_order;
		nano::mutex mutex;
		add_callback_stats (*node, &observer_order, &mutex);

		auto election = nano::test::start_election (system, *node, open1->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();
		ASSERT_TIMELY (5s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 10);

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
					.representative (nano::dev::genesis->account ())
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
					 .representative (nano::dev::genesis->account ())
					 .account (key2.pub)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build_shared ();

		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *dummy_send).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive1).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send2).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *receive2).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *dummy_send1).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send3).code);
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *dummy_send2).code);

			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *open1).code);
		}

		// Force some blocks to be cemented so that the cached confirmed info variable is empty
		{
			auto transaction (node->store.tx_begin_write ());
			node->store.confirmation_height.put (transaction, nano::dev::genesis->account (), nano::confirmation_height_info{ 3, send1->hash () });
			node->store.confirmation_height.put (transaction, key1.pub, nano::confirmation_height_info{ 2, receive1->hash () });
		}

		add_callback_stats (*node);

		auto election = nano::test::start_election (system, *node, open1->hash ());
		ASSERT_NE (nullptr, election);
		election->force_confirm ();
		ASSERT_TIMELY (5s, node->stats.count (nano::stat::type::http_callback, nano::stat::detail::http_callback, nano::stat::dir::out) == 6);

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

TEST (confirmation_height, election_winner_details_clearing)
{
	auto test_mode = [] (nano::confirmation_height_mode mode_a) {
		nano::test::system system{};

		nano::node_flags node_flags{};
		node_flags.confirmation_height_processor_mode = mode_a;

		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;

		auto node = system.add_node (node_config, node_flags);
		auto const latest = node->latest (nano::dev::genesis_key.pub);

		nano::keypair key1{};
		nano::send_block_builder builder{};

		auto const send1 = builder.make_block ()
						   .previous (latest)
						   .destination (key1.pub)
						   .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
						   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						   .work (*system.work.generate (latest))
						   .build_shared ();
		ASSERT_EQ (nano::process_result::progress, node->process (*send1).code);

		auto const send2 = builder.make_block ()
						   .previous (send1->hash ())
						   .destination (key1.pub)
						   .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2)
						   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						   .work (*system.work.generate (send1->hash ()))
						   .build_shared ();
		ASSERT_EQ (nano::process_result::progress, node->process (*send2).code);

		auto const send3 = builder.make_block ()
						   .previous (send2->hash ())
						   .destination (key1.pub)
						   .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 3)
						   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						   .work (*system.work.generate (send2->hash ()))
						   .build_shared ();
		ASSERT_EQ (nano::process_result::progress, node->process (*send3).code);

		node->process_confirmed (nano::election_status{ send2 });
		ASSERT_TIMELY (5s, node->block_confirmed (send2->hash ()));
		ASSERT_TIMELY (5s, 1 == node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));

		node->process_confirmed (nano::election_status{ send3 });
		ASSERT_TIMELY (5s, node->block_confirmed (send3->hash ()));

		// Add an already cemented block with fake election details. It should get removed
		node->active.add_election_winner_details (send3->hash (), nullptr);
		node->confirmation_height_processor.add (send3);
		ASSERT_TIMELY (10s, node->active.election_winner_details_size () == 0);
		ASSERT_EQ (4, node->ledger.cache.cemented_count);

		EXPECT_EQ (1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
		EXPECT_EQ (3, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in));
		EXPECT_EQ (3, node->stats.count (nano::stat::type::confirmation_height, get_stats_detail (mode_a), nano::stat::dir::in));
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
				.build_shared ();
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
	nano::logger_mt logger;
	auto path (nano::unique_path ());
	auto store = nano::make_store (logger, path, nano::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	nano::stats stats;
	nano::ledger ledger (*store, stats, nano::dev::constants);
	nano::write_database_queue write_database_queue (false);
	boost::latch initialized_latch{ 0 };
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::logging logging;
	nano::keypair key1;
	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (nano::dev::genesis->hash ())
				.destination (key1.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*pool.generate (nano::dev::genesis->hash ()))
				.build_shared ();
	auto send1 = builder
				 .send ()
				 .previous (send->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio * 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send->hash ()))
				 .build_shared ();
	{
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, ledger.cache, nano::dev::constants);
		ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *send).code);
		ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *send1).code);
	}

	nano::confirmation_height_processor confirmation_height_processor (ledger, write_database_queue, 10ms, logging, logger, initialized_latch, nano::confirmation_height_mode::unbounded);
	nano::timer<> timer;
	timer.start ();
	{
		// Prevent conf height processor doing any writes, so that we can query is_processing_block correctly
		auto write_guard = write_database_queue.wait (nano::writer::testing);
		// Add the frontier block
		confirmation_height_processor.add (send1);

		// The most uncemented block (previous block) should be seen as processing by the unbounded processor
		while (!confirmation_height_processor.is_processing_block (send->hash ()))
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

TEST (confirmation_height, pruned_source)
{
	nano::logger_mt logger;
	nano::logging logging;
	auto path (nano::unique_path ());
	auto store = nano::make_store (logger, path, nano::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	nano::stats stats;
	nano::ledger ledger (*store, stats, nano::dev::constants);
	ledger.pruning = true;
	nano::write_database_queue write_database_queue (false);
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::keypair key1, key2;
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	auto open1 = builder
				 .state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build_shared ();
	auto send2 = builder
				 .state ()
				 .account (key1.pub)
				 .previous (open1->hash ())
				 .representative (key1.pub)
				 .balance (50)
				 .link (key2.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (open1->hash ()))
				 .build_shared ();
	auto send3 = builder
				 .state ()
				 .account (key1.pub)
				 .previous (send2->hash ())
				 .representative (key1.pub)
				 .balance (25)
				 .link (key2.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (send2->hash ()))
				 .build_shared ();
	auto open2 = builder
				 .state ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (50)
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*pool.generate (key2.pub))
				 .build_shared ();
	{
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, ledger.cache, nano::dev::constants);
		ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *send1).code);
		ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *open1).code);
		ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *send2).code);
		ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *send3).code);
		ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, *open2).code);
	}
	uint64_t batch_write_size = 2;
	std::atomic<bool> stopped{ false };
	bool first_time{ true };
	nano::confirmation_height_bounded bounded_processor (
	ledger, write_database_queue, 10ms, logging, logger, stopped, batch_write_size, [&] (auto const & cemented_blocks_a) {
		if (first_time)
		{
			// Prune the send
			auto transaction (store->tx_begin_write ());
			ASSERT_EQ (2, ledger.pruning_action (transaction, send2->hash (), 2));
		}
		first_time = false; },
	[] (auto const &) {}, [] () { return 0; });
	bounded_processor.process (open2);
}
