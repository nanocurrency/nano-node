#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/inactive_node.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/versioning.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace
{
nano::wallets make_wallets (nano::node & node)
{
	return nano::wallets (node, node.wallets_store, node.ledger, node.config, node.network_params, node.online_reps, node.network, node.logger);
}
}

TEST (wallets, open_create)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto wallets = make_wallets (node);
	ASSERT_EQ (1, wallets.get_wallets ().size ()); // it starts out with a default wallet
	auto id = nano::random_wallet_id ();
	ASSERT_EQ (nullptr, wallets.get (id));
	auto wallet (wallets.create (id));
	ASSERT_NE (nullptr, wallet);
	ASSERT_EQ (wallet, wallets.get (id));
}

TEST (wallets, open_existing)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto id (nano::random_wallet_id ());
	{
		auto wallets = make_wallets (node);
		ASSERT_EQ (1, wallets.get_wallets ().size ());
		auto wallet (wallets.create (id));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (wallet, wallets.get (id));
		nano::raw_key password;
		password.clear ();
		system.deadline_set (10s);
		while (password == 0)
		{
			ASSERT_NO_ERROR (system.poll ());
			wallet->store.password.value (password);
		}
	}
	{
		auto wallets = make_wallets (node);
		ASSERT_EQ (2, wallets.get_wallets ().size ());
		ASSERT_NE (nullptr, wallets.get (id));
	}
}

TEST (wallets, remove)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::wallet_id one (1);
	{
		auto wallets = make_wallets (node);
		ASSERT_EQ (1, wallets.get_wallets ().size ());
		auto wallet (wallets.create (one));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (2, wallets.get_wallets ().size ());
		wallets.destroy (one);
		ASSERT_EQ (1, wallets.get_wallets ().size ());
	}
	{
		auto wallets = make_wallets (node);
		ASSERT_EQ (1, wallets.get_wallets ().size ());
	}
}

// Opening multiple environments using the same file within the same process is not supported.
// http://www.lmdb.tech/doc/starting.html
TEST (wallets, DISABLED_reload)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::wallet_id one (1);
	bool error (false);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, node1.wallets.get_wallets ().size ());
	{
		nano::inactive_node node (node1.application_path, nano::inactive_node_flag_defaults ());
		auto wallet (node.node->wallets.create (one));
		ASSERT_NE (wallet, nullptr);
	}
	ASSERT_TIMELY (5s, node1.wallets.get (one) != nullptr);
	ASSERT_EQ (2, node1.wallets.get_wallets ().size ());
}

TEST (wallets, vote_minimum)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::keypair key1;
	nano::keypair key2;
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - node1.config.vote_minimum.number ())
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (send1));
	auto open1 = builder
				 .state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (node1.config.vote_minimum.number ())
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (open1));
	// send2 with amount vote_minimum - 1 (not voting representative)
	auto send2 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (std::numeric_limits<nano::uint128_t>::max () - 2 * node1.config.vote_minimum.number () + 1)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (send2));
	auto open2 = builder
				 .state ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (key2.pub)
				 .balance (node1.config.vote_minimum.number () - 1)
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (key2.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (open2));
	auto wallet (node1.wallets.get_wallets ().begin ()->second);
	ASSERT_EQ (0, wallet->representatives->size ());
	wallet->insert_adhoc (nano::dev::genesis_key.prv);
	wallet->insert_adhoc (key1.prv);
	wallet->insert_adhoc (key2.prv);
	node1.wallets.compute_reps ();
	ASSERT_EQ (2, wallet->representatives->size ());
}

TEST (wallets, exists)
{
	nano::test::system system (1);
	auto & node (*system.nodes[0]);
	nano::keypair key1;
	nano::keypair key2;
	{
		auto transaction (node.wallets.tx_begin_read ());
		ASSERT_FALSE (node.wallets.exists (transaction, key1.pub));
		ASSERT_FALSE (node.wallets.exists (transaction, key2.pub));
	}
	system.wallet (0)->insert_adhoc (key1.prv);
	{
		auto transaction (node.wallets.tx_begin_read ());
		ASSERT_TRUE (node.wallets.exists (transaction, key1.pub));
		ASSERT_FALSE (node.wallets.exists (transaction, key2.pub));
	}
	system.wallet (0)->insert_adhoc (key2.prv);
	{
		auto transaction (node.wallets.tx_begin_read ());
		ASSERT_TRUE (node.wallets.exists (transaction, key1.pub));
		ASSERT_TRUE (node.wallets.exists (transaction, key2.pub));
	}
}

TEST (wallets, search_receivable)
{
	for (auto search_all : { false, true })
	{
		nano::test::system system;
		nano::node_config config = system.default_config ();
		config.enable_voting = false;
		config.backlog_scan.enable = false;
		nano::node_flags flags;
		flags.disable_search_pending = true;
		auto & node (*system.add_node (config, flags));

		auto wallets = node.wallets.get_wallets ();
		ASSERT_EQ (1, wallets.size ());
		auto wallet_id = wallets.begin ()->first;
		auto wallet = wallets.begin ()->second;

		wallet->insert_adhoc (nano::dev::genesis_key.prv);
		nano::block_builder builder;
		auto send = builder.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (nano::dev::genesis->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - node.config.receive_minimum.number ())
					.link (nano::dev::genesis_key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (nano::dev::genesis->hash ()))
					.build ();
		ASSERT_EQ (nano::block_status::progress, node.process (send));

		// Pending search should start an election
		ASSERT_TRUE (node.active.empty ());
		if (search_all)
		{
			node.wallets.search_receivable_all ();
		}
		else
		{
			node.wallets.search_receivable (wallet_id);
		}
		std::shared_ptr<nano::election> election;
		ASSERT_TIMELY (5s, election = node.active.election (send->qualified_root ()));

		// Erase the key so the confirmation does not trigger an automatic receive
		wallet->store.erase (node.wallets.tx_begin_write (), nano::dev::genesis_key.pub);

		// Now confirm the election
		election->force_confirm ();

		ASSERT_TIMELY (5s, node.block_confirmed (send->hash ()) && node.active.empty ());

		// Re-insert the key
		wallet->insert_adhoc (nano::dev::genesis_key.prv);

		// Pending search should create the receive block
		ASSERT_EQ (2, node.ledger.block_count ());
		if (search_all)
		{
			node.wallets.search_receivable_all ();
		}
		else
		{
			node.wallets.search_receivable (wallet_id);
		}
		ASSERT_TIMELY_EQ (3s, node.balance (nano::dev::genesis_key.pub), nano::dev::constants.genesis_amount);
		auto receive_hash = node.ledger.any.account_head (node.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
		auto receive = node.block (receive_hash);
		ASSERT_NE (nullptr, receive);
		ASSERT_EQ (receive->sideband ().height, 3);
		ASSERT_EQ (send->hash (), receive->source ());
	}
}

// Test that local rep scan is called during wallets construction and correctly identifies representatives
TEST (wallets, local_reps)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	// Get the default wallet
	auto wallet = node.wallets.get_wallets ().begin ()->second;

	// Insert genesis key - it has massive weight, should set half_principal = true
	wallet->insert_adhoc (nano::dev::genesis_key.prv);

	// Create an account with exactly vote_minimum balance
	nano::keypair exact_minimum_key;
	nano::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - node.config.vote_minimum.number ())
				 .link (exact_minimum_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send1));

	auto open1 = builder
				 .state ()
				 .account (exact_minimum_key.pub)
				 .previous (0)
				 .representative (exact_minimum_key.pub)
				 .balance (node.config.vote_minimum.number ())
				 .link (send1->hash ())
				 .sign (exact_minimum_key.prv, exact_minimum_key.pub)
				 .work (*system.work.generate (exact_minimum_key.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open1));

	// Create an account with vote_minimum - 1 balance (should NOT qualify)
	nano::keypair below_minimum_key;
	auto send2 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * node.config.vote_minimum.number () + 1)
				 .link (below_minimum_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send2));

	auto open2 = builder
				 .state ()
				 .account (below_minimum_key.pub)
				 .previous (0)
				 .representative (below_minimum_key.pub)
				 .balance (node.config.vote_minimum.number () - 1)
				 .link (send2->hash ())
				 .sign (below_minimum_key.prv, below_minimum_key.pub)
				 .work (*system.work.generate (below_minimum_key.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open2));

	// Insert both keys into wallet
	wallet->insert_adhoc (exact_minimum_key.prv);
	wallet->insert_adhoc (below_minimum_key.prv);

	// Re-construct wallets
	auto wallets = make_wallets (node);

	// Verify representatives were correctly identified
	auto reps = wallets.reps ();
	ASSERT_EQ (2, reps.voting); // genesis + exact_minimum_key
	ASSERT_TRUE (reps.accounts.contains (nano::dev::genesis_key.pub));
	ASSERT_TRUE (reps.accounts.contains (exact_minimum_key.pub));
	ASSERT_FALSE (reps.accounts.contains (below_minimum_key.pub));
	ASSERT_TRUE (reps.half_principal); // genesis has massive weight

	// Verify per-wallet representatives
	auto wallet_after = wallets.get_wallets ().begin ()->second;
	ASSERT_EQ (2, wallet_after->representatives->size ());
	ASSERT_EQ (1, wallet_after->representatives->count (nano::dev::genesis_key.pub));
	ASSERT_EQ (1, wallet_after->representatives->count (exact_minimum_key.pub));
	ASSERT_EQ (0, wallet_after->representatives->count (below_minimum_key.pub));
}

// Test that rep scan correctly handles multiple wallets and tracks representatives per-wallet
TEST (wallets, local_reps_multiple_wallets)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	// Create two accounts with vote_minimum balance
	nano::keypair key1;
	nano::keypair key2;
	nano::block_builder builder;

	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - node.config.vote_minimum.number ())
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send1));

	auto open1 = builder
				 .state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (node.config.vote_minimum.number ())
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open1));

	auto send2 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * node.config.vote_minimum.number ())
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send2));

	auto open2 = builder
				 .state ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (key2.pub)
				 .balance (node.config.vote_minimum.number ())
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (key2.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open2));

	// Create additional wallets
	auto wallet1 = node.wallets.get_wallets ().begin ()->second;
	auto wallet2_id = nano::random_wallet_id ();
	auto wallet2 = node.wallets.create (wallet2_id);
	auto wallet3_id = nano::random_wallet_id ();
	auto wallet3 = node.wallets.create (wallet3_id);

	// Insert key1 into wallet1, key2 into wallet2 (wallet3 remains empty)
	wallet1->insert_adhoc (key1.prv);
	wallet2->insert_adhoc (key2.prv);

	// Verify global reps
	auto reps = node.wallets.reps ();
	ASSERT_EQ (2, reps.voting);
	ASSERT_TRUE (reps.accounts.contains (key1.pub));
	ASSERT_TRUE (reps.accounts.contains (key2.pub));

	// Verify per-wallet representatives
	ASSERT_EQ (1, wallet1->representatives->size ());
	ASSERT_EQ (1, wallet1->representatives->count (key1.pub));
	ASSERT_EQ (0, wallet1->representatives->count (key2.pub));

	ASSERT_EQ (1, wallet2->representatives->size ());
	ASSERT_EQ (1, wallet2->representatives->count (key2.pub));
	ASSERT_EQ (0, wallet2->representatives->count (key1.pub));

	ASSERT_TRUE (wallet3->representatives->empty ());
}