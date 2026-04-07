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
	return nano::wallets (node, node.wallets_store, node.ledger, node.config, node.network_params, node.online_reps, node.network, node.stats, node.logger);
}
}

TEST (wallets, open_create)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	node.wallets.stop (); // Stop node wallets to avoid race condition with local wallets sharing same LMDB environment
	auto wallets = make_wallets (node);
	ASSERT_EQ (1, wallets.items.size ()); // it starts out with a default wallet
	auto id = nano::random_wallet_id ();
	ASSERT_EQ (nullptr, wallets.open (id));
	auto wallet (wallets.create (id));
	ASSERT_NE (nullptr, wallet);
	ASSERT_EQ (wallet, wallets.open (id));
}

TEST (wallets, open_existing)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	node.wallets.stop (); // Stop node wallets to avoid race condition with local wallets sharing same LMDB environment
	auto id (nano::random_wallet_id ());
	{
		auto wallets = make_wallets (node);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (id));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (wallet, wallets.open (id));
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
		ASSERT_EQ (2, wallets.items.size ());
		ASSERT_NE (nullptr, wallets.open (id));
	}
}

TEST (wallets, remove)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	node.wallets.stop (); // Stop node wallets to avoid race condition with local wallets sharing same LMDB environment
	nano::wallet_id one (1);
	{
		auto wallets = make_wallets (node);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (one));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (2, wallets.items.size ());
		wallets.destroy (one);
		ASSERT_EQ (1, wallets.items.size ());
	}
	{
		auto wallets = make_wallets (node);
		ASSERT_EQ (1, wallets.items.size ());
	}
}

TEST (wallets, create_from_json)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	node.wallets.stop (); // Stop node wallets to avoid race condition with local wallets sharing same LMDB environment
	nano::wallet_id id (1);
	std::string json;
	nano::public_key account;
	{
		auto wallets = make_wallets (node);
		auto wallet = wallets.create (id);
		ASSERT_NE (nullptr, wallet);
		account = wallet->deterministic_insert ();
		wallet->serialize_json (json);
		ASSERT_FALSE (json.empty ());
		wallets.destroy (id);
		ASSERT_EQ (nullptr, wallets.open (id));
	}
	{
		auto wallets = make_wallets (node);
		// Invalid JSON should return nullptr
		auto bad_wallet = wallets.create_from_json (id, "not valid json");
		ASSERT_EQ (nullptr, bad_wallet);
		ASSERT_EQ (nullptr, wallets.open (id));
		// Create wallet from exported JSON
		auto wallet = wallets.create_from_json (id, json);
		ASSERT_NE (nullptr, wallet);
		ASSERT_NE (nullptr, wallets.open (id));
		// Verify the account was restored
		ASSERT_TRUE (wallet->exists (account));
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
	ASSERT_EQ (1, node1.wallets.items.size ());
	{
		nano::lock_guard<nano::mutex> lock_wallet (node1.wallets.mutex);
		nano::inactive_node node (node1.application_path, nano::inactive_node_flag_defaults ());
		auto wallet (node.node->wallets.create (one));
		ASSERT_NE (wallet, nullptr);
	}
	ASSERT_TIMELY (5s, node1.wallets.open (one) != nullptr);
	ASSERT_EQ (2, node1.wallets.items.size ());
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
	auto wallet (node1.wallets.items.begin ()->second);
	ASSERT_EQ (0, wallet->reps ().size ());
	wallet->insert_adhoc (nano::dev::genesis_key.prv);
	wallet->insert_adhoc (key1.prv);
	wallet->insert_adhoc (key2.prv);
	node1.wallets.refresh_reps ();
	ASSERT_EQ (2, wallet->reps ().size ());
}

TEST (wallets, exists)
{
	nano::test::system system (1);
	auto & node (*system.nodes[0]);
	nano::keypair key1;
	nano::keypair key2;
	ASSERT_FALSE (node.wallets.exists (key1.pub));
	ASSERT_FALSE (node.wallets.exists (key2.pub));
	system.wallet (0)->insert_adhoc (key1.prv);
	ASSERT_TRUE (node.wallets.exists (key1.pub));
	ASSERT_FALSE (node.wallets.exists (key2.pub));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_TRUE (node.wallets.exists (key1.pub));
	ASSERT_TRUE (node.wallets.exists (key2.pub));
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

		auto wallets = node.wallets.all_wallets ();
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
		wallet->remove_account (nano::dev::genesis_key.pub);

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

// Test that the background reps scanning thread automatically detects new representatives
TEST (wallets, rep_scan)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	auto & node = *system.add_node (config);

	auto wallet = node.wallets.items.begin ()->second;

	// Insert a key that initially has no balance (not a representative)
	nano::keypair key;
	wallet->insert_adhoc (key.prv);

	// Initially the account should not be detected as a representative
	ASSERT_EQ (0, wallet->reps ().count (key.pub));

	// Send funds to make the account a representative (vote_minimum amount)
	nano::block_builder builder;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - node.config.vote_minimum.number ())
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	auto open = builder
				.state ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (node.config.vote_minimum.number ())
				.link (send->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open));

	// Clear stats and wait for the reps scan loop to run
	node.stats.clear ();
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::wallet, nano::stat::detail::loop_reps) > 1);

	// Verify that the wallet now detects the account as a representative
	ASSERT_TIMELY (5s, wallet->reps ().count (key.pub) == 1);

	// Also verify via the wallets reps() accessor
	auto reps = node.wallets.reps ();
	ASSERT_TRUE (reps.exists (key.pub));
}

// Test that the background receivable scanning thread automatically processes receivables
TEST (wallets, receivable_scan)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.backlog_scan.enable = false;
	auto & node = *system.add_node (config);

	auto wallet = node.wallets.items.begin ()->second;
	wallet->insert_adhoc (nano::dev::genesis_key.prv);

	// Create a send block to self (creates a receivable)
	nano::block_builder builder;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - node.config.receive_minimum.number ())
				.link (nano::dev::genesis_key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	// Confirm the send block (receivable scan only processes confirmed blocks)
	ASSERT_TIMELY (5s, node.block_confirmed (send->hash ()));

	// Clear stats and wait for the receivable scan loop to run
	node.stats.clear ();
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::wallet, nano::stat::detail::loop_receivable) > 1);

	// Verify the background scan automatically created the receive block
	ASSERT_TIMELY_EQ (5s, node.balance (nano::dev::genesis_key.pub), nano::dev::constants.genesis_amount);

	// Verify the receive block exists
	auto receive_hash = node.ledger.any.account_head (node.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	auto receive = node.block (receive_hash);
	ASSERT_NE (nullptr, receive);
	ASSERT_EQ (receive->sideband ().height, 3);
	ASSERT_EQ (send->hash (), receive->source ());
}

/*
 * Signer with no representatives should not invoke the callback
 */
TEST (wallets, signer_none)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	auto sign = node.wallets.signer ();

	int called = 0;
	sign ([&] (nano::public_key const &, nano::raw_key const &) {
		++called;
	});
	ASSERT_EQ (0, called);
}

/*
 * Signer should invoke the callback for each representative with voting weight
 */
TEST (wallets, signer_multiple)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	nano::keypair rep2;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (rep2.prv);

	auto const amount = 100 * nano::Knano_ratio;
	system.wallet (0)->send_sync (nano::dev::genesis_key.pub, rep2.pub, amount);
	ASSERT_TIMELY (5s, node.balance (rep2.pub) == amount);
	system.wallet (0)->change_sync (rep2.pub, rep2.pub);
	node.wallets.refresh_reps ();
	ASSERT_EQ (2, node.wallets.reps ().voting);

	auto sign = node.wallets.signer ();

	std::set<nano::account> accounts;
	sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
		accounts.insert (pub);
	});

	ASSERT_EQ (2, accounts.size ());
	ASSERT_TRUE (accounts.count (nano::dev::genesis_key.pub));
	ASSERT_TRUE (accounts.count (rep2.pub));
}

/*
 * Signer should skip representatives with zero weight or weight below vote_minimum
 */
TEST (wallets, signer_below_weight)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.vote_minimum = nano::Knano_ratio; // 1000 nano
	auto & node = *system.add_node (config);

	nano::keypair zero_weight;
	nano::keypair below_minimum;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (zero_weight.prv);
	system.wallet (0)->insert_adhoc (below_minimum.prv);

	// Give below_minimum account less than vote_minimum
	auto const amount = nano::Knano_ratio - 1;
	system.wallet (0)->send_sync (nano::dev::genesis_key.pub, below_minimum.pub, amount);
	ASSERT_TIMELY (5s, node.balance (below_minimum.pub) == amount);
	system.wallet (0)->change_sync (below_minimum.pub, below_minimum.pub);

	ASSERT_EQ (0, node.weight (zero_weight.pub));
	ASSERT_TRUE (node.weight (below_minimum.pub) > 0);
	ASSERT_TRUE (node.weight (below_minimum.pub) < config.vote_minimum.number ());

	node.wallets.refresh_reps ();
	ASSERT_EQ (1, node.wallets.reps ().voting);

	auto sign = node.wallets.signer ();

	std::set<nano::account> accounts;
	sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
		accounts.insert (pub);
	});

	// Only genesis should sign, others are below threshold
	ASSERT_EQ (1, accounts.size ());
	ASSERT_TRUE (accounts.count (nano::dev::genesis_key.pub));
	ASSERT_FALSE (accounts.count (zero_weight.pub));
	ASSERT_FALSE (accounts.count (below_minimum.pub));
}

/*
 * rep_keys_cache tests
 */

/*
 * Locking a wallet should clear its reps from the cache
 */
TEST (wallets, rep_keys_cache_lock_clears)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	// Pre-condition: genesis key is in cache
	{
		auto sign = node.wallets.signer ();
		int called = 0;
		sign ([&] (nano::public_key const &, nano::raw_key const &) {
			++called;
		});
		ASSERT_EQ (1, called);
	}

	// Lock the wallet
	system.wallet (0)->rekey ("pass");
	system.wallet (0)->lock ();
	ASSERT_TRUE (system.wallet (0)->is_locked ());

	// Cache should be empty
	auto sign = node.wallets.signer ();
	int called = 0;
	sign ([&] (nano::public_key const &, nano::raw_key const &) {
		++called;
	});
	ASSERT_EQ (0, called);
}

/*
 * Unlocking a wallet should repopulate the cache
 */
TEST (wallets, rep_keys_cache_unlock_repopulates)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	// Lock the wallet
	system.wallet (0)->rekey ("pass");
	system.wallet (0)->lock ();

	// Pre-condition: cache is empty
	{
		auto sign = node.wallets.signer ();
		int called = 0;
		sign ([&] (nano::public_key const &, nano::raw_key const &) {
			++called;
		});
		ASSERT_EQ (0, called);
	}

	// Unlock the wallet
	ASSERT_FALSE (system.wallet (0)->enter_password ("pass"));
	ASSERT_FALSE (system.wallet (0)->is_locked ());

	// Cache should be repopulated
	auto sign = node.wallets.signer ();
	std::set<nano::account> accounts;
	sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
		accounts.insert (pub);
	});
	ASSERT_EQ (1, accounts.size ());
	ASSERT_TRUE (accounts.count (nano::dev::genesis_key.pub));
}

/*
 * Rekeying a wallet should preserve cached keys (wallet stays unlocked)
 */
TEST (wallets, rep_keys_cache_rekey_preserves)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	// Capture key pair before rekey
	nano::public_key pub_before;
	nano::raw_key prv_before;
	{
		auto sign = node.wallets.signer ();
		sign ([&] (nano::public_key const & pub, nano::raw_key const & prv) {
			pub_before = pub;
			prv_before = prv;
		});
	}
	ASSERT_EQ (nano::dev::genesis_key.pub, pub_before);

	// Rekey
	ASSERT_FALSE (system.wallet (0)->rekey ("newpass"));
	ASSERT_FALSE (system.wallet (0)->is_locked ());

	// Capture key pair after rekey — should be identical
	nano::public_key pub_after;
	nano::raw_key prv_after;
	{
		auto sign = node.wallets.signer ();
		sign ([&] (nano::public_key const & pub, nano::raw_key const & prv) {
			pub_after = pub;
			prv_after = prv;
		});
	}
	ASSERT_EQ (pub_before, pub_after);
	ASSERT_EQ (prv_before, prv_after);
	ASSERT_EQ (nano::dev::genesis_key.prv, prv_after);
}

/*
 * insert_adhoc of a key with rep-level weight should immediately update the cache
 * without needing an explicit refresh_reps() call
 */
TEST (wallets, rep_keys_cache_insert_adhoc_qualifying)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Create a second account with vote_minimum weight
	nano::keypair rep2;
	nano::block_builder builder;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - node.config.vote_minimum.number ())
				.link (rep2.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	auto open = builder
				.state ()
				.account (rep2.pub)
				.previous (0)
				.representative (rep2.pub)
				.balance (node.config.vote_minimum.number ())
				.link (send->hash ())
				.sign (rep2.prv, rep2.pub)
				.work (*system.work.generate (rep2.pub))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open));

	node.wallets.refresh_reps ();

	// Pre-condition: only genesis in cache
	{
		auto sign = node.wallets.signer ();
		std::set<nano::account> accounts;
		sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
			accounts.insert (pub);
		});
		ASSERT_EQ (1, accounts.size ());
		ASSERT_TRUE (accounts.count (nano::dev::genesis_key.pub));
	}

	// Insert rep2 — should immediately appear in cache (no refresh_reps needed)
	system.wallet (0)->insert_adhoc (rep2.prv);

	auto sign = node.wallets.signer ();
	std::set<nano::account> accounts;
	sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
		accounts.insert (pub);
	});
	ASSERT_EQ (2, accounts.size ());
	ASSERT_TRUE (accounts.count (nano::dev::genesis_key.pub));
	ASSERT_TRUE (accounts.count (rep2.pub));
}

/*
 * insert_adhoc of a zero-weight key should not modify the cache
 */
TEST (wallets, rep_keys_cache_insert_adhoc_nonqualifying)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	// Insert a key with no weight
	nano::keypair nobody;
	system.wallet (0)->insert_adhoc (nobody.prv);

	// Cache should still contain only genesis
	auto sign = node.wallets.signer ();
	std::set<nano::account> accounts;
	sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
		accounts.insert (pub);
	});
	ASSERT_EQ (1, accounts.size ());
	ASSERT_TRUE (accounts.count (nano::dev::genesis_key.pub));
	ASSERT_FALSE (accounts.count (nobody.pub));
}

/*
 * With two wallets, locking one should only remove that wallet's reps from cache
 */
TEST (wallets, rep_keys_cache_multiple_wallets_partial_lock)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	// Wallet 1: genesis key
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Create rep2 with voting weight
	nano::keypair rep2;
	nano::block_builder builder;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - node.config.vote_minimum.number ())
				.link (rep2.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	auto open = builder
				.state ()
				.account (rep2.pub)
				.previous (0)
				.representative (rep2.pub)
				.balance (node.config.vote_minimum.number ())
				.link (send->hash ())
				.sign (rep2.prv, rep2.pub)
				.work (*system.work.generate (rep2.pub))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open));

	// Wallet 2: rep2 key
	auto wallet2 = node.wallets.create (nano::random_wallet_id ());
	wallet2->insert_adhoc (rep2.prv);

	node.wallets.refresh_reps ();

	// Pre-condition: both reps in cache
	{
		auto sign = node.wallets.signer ();
		std::set<nano::account> accounts;
		sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
			accounts.insert (pub);
		});
		ASSERT_EQ (2, accounts.size ());
		ASSERT_TRUE (accounts.count (nano::dev::genesis_key.pub));
		ASSERT_TRUE (accounts.count (rep2.pub));
	}

	// Lock wallet2 only
	wallet2->rekey ("pass");
	wallet2->lock ();

	// Only wallet1's rep should remain
	auto sign = node.wallets.signer ();
	std::set<nano::account> accounts;
	sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
		accounts.insert (pub);
	});
	ASSERT_EQ (1, accounts.size ());
	ASSERT_TRUE (accounts.count (nano::dev::genesis_key.pub));
	ASSERT_FALSE (accounts.count (rep2.pub));
}

/*
 * Private keys reconstructed from fan objects should derive to the correct public keys
 */
TEST (wallets, rep_keys_cache_key_correctness)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	auto sign = node.wallets.signer ();
	std::map<nano::public_key, nano::raw_key> key_pairs;
	sign ([&] (nano::public_key const & pub, nano::raw_key const & prv) {
		key_pairs[pub] = prv;
	});

	ASSERT_EQ (1, key_pairs.size ());
	ASSERT_TRUE (key_pairs.count (nano::dev::genesis_key.pub));

	// Verify the cached private key matches the original
	ASSERT_EQ (nano::dev::genesis_key.prv, key_pairs[nano::dev::genesis_key.pub]);

	// Verify the private key derives back to the correct public key
	ASSERT_EQ (nano::dev::genesis_key.pub, nano::pub_key (key_pairs[nano::dev::genesis_key.pub]));
}

/*
 * With voting disabled, foreach_representative should never invoke the callback
 */
TEST (wallets, rep_keys_cache_voting_disabled)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = false;
	auto & node = *system.add_node (config);

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	auto sign = node.wallets.signer ();
	int called = 0;
	sign ([&] (nano::public_key const &, nano::raw_key const &) {
		++called;
	});
	ASSERT_EQ (0, called);
}

/*
 * After removing an account from a wallet, refresh_reps should exclude it from the cache
 */
TEST (wallets, rep_keys_cache_account_removed)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	// Pre-condition: genesis in cache
	{
		auto sign = node.wallets.signer ();
		int called = 0;
		sign ([&] (nano::public_key const &, nano::raw_key const &) {
			++called;
		});
		ASSERT_EQ (1, called);
	}

	// Remove account and refresh
	system.wallet (0)->remove_account (nano::dev::genesis_key.pub);
	node.wallets.refresh_reps ();

	// Cache should be empty
	auto sign = node.wallets.signer ();
	int called = 0;
	sign ([&] (nano::public_key const &, nano::raw_key const &) {
		++called;
	});
	ASSERT_EQ (0, called);
}

/*
 * A rep that loses all weight should be excluded from the cache on next refresh
 */
TEST (wallets, rep_keys_cache_weight_lost)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Create rep2 with vote_minimum weight
	nano::keypair rep2;
	nano::block_builder builder;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - node.config.vote_minimum.number ())
				.link (rep2.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	auto open = builder
				.state ()
				.account (rep2.pub)
				.previous (0)
				.representative (rep2.pub)
				.balance (node.config.vote_minimum.number ())
				.link (send->hash ())
				.sign (rep2.prv, rep2.pub)
				.work (*system.work.generate (rep2.pub))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open));

	system.wallet (0)->insert_adhoc (rep2.prv);
	node.wallets.refresh_reps ();

	// Pre-condition: both reps in cache
	{
		auto sign = node.wallets.signer ();
		std::set<nano::account> accounts;
		sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
			accounts.insert (pub);
		});
		ASSERT_EQ (2, accounts.size ());
	}

	// rep2 sends all balance back to genesis (weight drops to zero)
	auto send_back = builder
					 .state ()
					 .account (rep2.pub)
					 .previous (open->hash ())
					 .representative (rep2.pub)
					 .balance (0)
					 .link (nano::dev::genesis_key.pub)
					 .sign (rep2.prv, rep2.pub)
					 .work (*system.work.generate (open->hash ()))
					 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send_back));
	ASSERT_EQ (0, node.weight (rep2.pub));

	node.wallets.refresh_reps ();

	// Only genesis should remain
	auto sign = node.wallets.signer ();
	std::set<nano::account> accounts;
	sign ([&] (nano::public_key const & pub, nano::raw_key const &) {
		accounts.insert (pub);
	});
	ASSERT_EQ (1, accounts.size ());
	ASSERT_TRUE (accounts.count (nano::dev::genesis_key.pub));
	ASSERT_FALSE (accounts.count (rep2.pub));
}