#include <gtest/gtest.h>

#include <nano/core_test/testutil.hpp>
#include <nano/node/node.hpp>
#include <nano/node/testing.hpp>

#include <boost/polymorphic_cast.hpp>

using namespace std::chrono_literals;

TEST (wallets, open_create)
{
	nano::system system (24000, 1);
	bool error (false);
	nano::wallets wallets (error, *system.nodes[0]);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, wallets.items.size ()); // it starts out with a default wallet
	nano::uint256_union id;
	ASSERT_EQ (nullptr, wallets.open (id));
	auto wallet (wallets.create (id));
	ASSERT_NE (nullptr, wallet);
	ASSERT_EQ (wallet, wallets.open (id));
}

TEST (wallets, open_existing)
{
	nano::system system (24000, 1);
	nano::uint256_union id;
	{
		bool error (false);
		nano::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (id));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (wallet, wallets.open (id));
		nano::raw_key password;
		password.data.clear ();
		system.deadline_set (10s);
		while (password.data == 0)
		{
			ASSERT_NO_ERROR (system.poll ());
			wallet->store.password.value (password);
		}
	}
	{
		bool error (false);
		nano::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (2, wallets.items.size ());
		ASSERT_NE (nullptr, wallets.open (id));
	}
}

TEST (wallets, remove)
{
	nano::system system (24000, 1);
	nano::uint256_union one (1);
	{
		bool error (false);
		nano::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (one));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (2, wallets.items.size ());
		wallets.destroy (one);
		ASSERT_EQ (1, wallets.items.size ());
	}
	{
		bool error (false);
		nano::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
	}
}

TEST (wallets, upgrade)
{
	nano::system system (24000, 1);
	auto path (nano::unique_path ());
	nano::keypair id;
	{
		nano::node_init init1;
		auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, path, system.alarm, system.logging, system.work));
		ASSERT_FALSE (init1.error ());
		node1->wallets.create (id.pub);
		auto transaction_source (node1->wallets.env.tx_begin (true));
		MDB_txn * tx_source (*boost::polymorphic_downcast<nano::mdb_txn *> (transaction_source.impl.get ()));
		auto & mdb_store (dynamic_cast<nano::mdb_store &> (node1->store));
		auto transaction_destination (mdb_store.tx_begin_write ());
		MDB_txn * tx_destination (*boost::polymorphic_downcast<nano::mdb_txn *> (transaction_destination.impl.get ()));
		node1->wallets.move_table (id.pub.to_string (), tx_source, tx_destination);
		node1->store.version_put (transaction_destination, 11);
	}
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, path, system.alarm, system.logging, system.work));
	ASSERT_EQ (1, node1->wallets.items.size ());
	ASSERT_EQ (id.pub, node1->wallets.items.begin ()->first);
	auto transaction_new (node1->wallets.env.tx_begin (true));
	MDB_txn * tx_new (*boost::polymorphic_downcast<nano::mdb_txn *> (transaction_new.impl.get ()));
	auto transaction_old (node1->store.tx_begin_write ());
	MDB_txn * tx_old (*boost::polymorphic_downcast<nano::mdb_txn *> (transaction_old.impl.get ()));
	MDB_dbi old_handle;
	ASSERT_EQ (MDB_NOTFOUND, mdb_dbi_open (tx_old, id.pub.to_string ().c_str (), 0, &old_handle));
	MDB_dbi new_handle;
	ASSERT_EQ (0, mdb_dbi_open (tx_new, id.pub.to_string ().c_str (), 0, &new_handle));
}

// Keeps breaking whenever we add new DBs
TEST (wallets, DISABLED_wallet_create_max)
{
	nano::system system (24000, 1);
	bool error (false);
	nano::wallets wallets (error, *system.nodes[0]);
	const int nonWalletDbs = 19;
	for (int i = 0; i < system.nodes[0]->config.lmdb_max_dbs - nonWalletDbs; i++)
	{
		nano::keypair key;
		auto wallet = wallets.create (key.pub);
		auto existing = wallets.items.find (key.pub);
		ASSERT_TRUE (existing != wallets.items.end ());
		nano::raw_key seed;
		seed.data = 0;
		auto transaction (system.nodes[0]->store.tx_begin (true));
		existing->second->store.seed_set (transaction, seed);
	}
	nano::keypair key;
	wallets.create (key.pub);
	auto existing = wallets.items.find (key.pub);
	ASSERT_TRUE (existing == wallets.items.end ());
}

TEST (wallets, reload)
{
	nano::system system (24000, 1);
	nano::uint256_union one (1);
	bool error (false);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, system.nodes[0]->wallets.items.size ());
	{
		nano::inactive_node node (system.nodes[0]->application_path, 24001);
		auto wallet (node.node->wallets.create (one));
		ASSERT_NE (wallet, nullptr);
	}
	system.deadline_set (5s);
	while (system.nodes[0]->wallets.open (one) == nullptr)
	{
		system.poll ();
	}
	ASSERT_EQ (2, system.nodes[0]->wallets.items.size ());
}

TEST (wallets, vote_minimum)
{
	nano::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	nano::keypair key1;
	nano::keypair key2;
	nano::genesis genesis;
	nano::state_block send1 (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, std::numeric_limits<nano::uint128_t>::max () - node1.config.vote_minimum.number (), key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (genesis.hash ()));
	ASSERT_EQ (nano::process_result::progress, node1.process (send1).code);
	nano::state_block open1 (key1.pub, 0, key1.pub, node1.config.vote_minimum.number (), send1.hash (), key1.prv, key1.pub, system.work.generate (key1.pub));
	ASSERT_EQ (nano::process_result::progress, node1.process (open1).code);
	// send2 with amount vote_minimum - 1 (not voting representative)
	nano::state_block send2 (nano::test_genesis_key.pub, send1.hash (), nano::test_genesis_key.pub, std::numeric_limits<nano::uint128_t>::max () - 2 * node1.config.vote_minimum.number () + 1, key2.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1.hash ()));
	ASSERT_EQ (nano::process_result::progress, node1.process (send2).code);
	nano::state_block open2 (key2.pub, 0, key2.pub, node1.config.vote_minimum.number () - 1, send2.hash (), key2.prv, key2.pub, system.work.generate (key2.pub));
	ASSERT_EQ (nano::process_result::progress, node1.process (open2).code);
	auto wallet (node1.wallets.items.begin ()->second);
	ASSERT_EQ (0, wallet->representatives.size ());
	wallet->insert_adhoc (nano::test_genesis_key.prv);
	wallet->insert_adhoc (key1.prv);
	wallet->insert_adhoc (key2.prv);
	node1.wallets.compute_reps ();
	ASSERT_EQ (2, wallet->representatives.size ());
}
