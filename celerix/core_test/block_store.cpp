#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/block_type.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/files.hpp>
#include <celerix/lib/lmdbconfig.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/lib/work.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/node/make_store.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/utility.hpp>
#include <celerix/store/account.hpp>
#include <celerix/store/block.hpp>
#include <celerix/store/lmdb/lmdb.hpp>
#include <celerix/store/rocksdb/rocksdb.hpp>
#include <celerix/store/versioning.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <unordered_set>
#include <vector>

using namespace std::chrono_literals;

TEST (block_store, construction)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
}

TEST (block_store, block_details)
{
	celerix::block_details details_send (celerix::epoch::epoch_0, true, false, false);
	ASSERT_TRUE (details_send.is_send);
	ASSERT_FALSE (details_send.is_receive);
	ASSERT_FALSE (details_send.is_epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, details_send.epoch);

	celerix::block_details details_receive (celerix::epoch::epoch_1, false, true, false);
	ASSERT_FALSE (details_receive.is_send);
	ASSERT_TRUE (details_receive.is_receive);
	ASSERT_FALSE (details_receive.is_epoch);
	ASSERT_EQ (celerix::epoch::epoch_1, details_receive.epoch);

	celerix::block_details details_epoch (celerix::epoch::epoch_2, false, false, true);
	ASSERT_FALSE (details_epoch.is_send);
	ASSERT_FALSE (details_epoch.is_receive);
	ASSERT_TRUE (details_epoch.is_epoch);
	ASSERT_EQ (celerix::epoch::epoch_2, details_epoch.epoch);

	celerix::block_details details_none (celerix::epoch::unspecified, false, false, false);
	ASSERT_FALSE (details_none.is_send);
	ASSERT_FALSE (details_none.is_receive);
	ASSERT_FALSE (details_none.is_epoch);
	ASSERT_EQ (celerix::epoch::unspecified, details_none.epoch);
}

TEST (block_store, block_details_serialization)
{
	celerix::block_details details1;
	details1.epoch = celerix::epoch::epoch_2;
	details1.is_epoch = false;
	details1.is_receive = true;
	details1.is_send = false;
	std::vector<uint8_t> vector;
	{
		celerix::vectorstream stream1 (vector);
		details1.serialize (stream1);
	}
	celerix::bufferstream stream2 (vector.data (), vector.size ());
	celerix::block_details details2;
	ASSERT_FALSE (details2.deserialize (stream2));
	ASSERT_EQ (details1, details2);
}

TEST (block_store, sideband_serialization)
{
	celerix::block_sideband sideband1;
	sideband1.account = 1;
	sideband1.balance = 2;
	sideband1.height = 3;
	sideband1.successor = 4;
	sideband1.timestamp = 5;
	std::vector<uint8_t> vector;
	{
		celerix::vectorstream stream1 (vector);
		sideband1.serialize (stream1, celerix::block_type::receive);
	}
	celerix::bufferstream stream2 (vector.data (), vector.size ());
	celerix::block_sideband sideband2;
	ASSERT_FALSE (sideband2.deserialize (stream2, celerix::block_type::receive));
	ASSERT_EQ (sideband1.account, sideband2.account);
	ASSERT_EQ (sideband1.balance, sideband2.balance);
	ASSERT_EQ (sideband1.height, sideband2.height);
	ASSERT_EQ (sideband1.successor, sideband2.successor);
	ASSERT_EQ (sideband1.timestamp, sideband2.timestamp);
}

TEST (block_store, add_item)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (0)
				 .sign (celerix::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	ASSERT_FALSE (store->block.exists (transaction, hash1));
	store->block.put (transaction, hash1, *block);
	auto latest2 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (*block, *latest2);
	ASSERT_TRUE (store->block.exists (transaction, hash1));
	ASSERT_FALSE (store->block.exists (transaction, hash1.number () - 1));
	store->block.del (transaction, hash1);
	auto latest3 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest3);
}

TEST (block_store, clear_successor)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (0)
				  .sign (celerix::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block1->sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, block1->hash (), *block1);
	auto block2 = builder
				  .open ()
				  .source (0)
				  .representative (2)
				  .account (0)
				  .sign (celerix::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block2->sideband_set ({});
	store->block.put (transaction, block2->hash (), *block2);
	auto block2_store (store->block.get (transaction, block1->hash ()));
	ASSERT_NE (nullptr, block2_store);
	ASSERT_EQ (0, block2_store->sideband ().successor.number ());
	auto modified_sideband = block2_store->sideband ();
	modified_sideband.successor = block2->hash ();
	block1->sideband_set (modified_sideband);
	store->block.put (transaction, block1->hash (), *block1);
	{
		auto block1_store (store->block.get (transaction, block1->hash ()));
		ASSERT_NE (nullptr, block1_store);
		ASSERT_EQ (block2->hash (), block1_store->sideband ().successor);
	}
	store->block.successor_clear (transaction, block1->hash ());
	{
		auto block1_store (store->block.get (transaction, block1->hash ()));
		ASSERT_NE (nullptr, block1_store);
		ASSERT_EQ (0, block1_store->sideband ().successor.number ());
	}
}

TEST (block_store, add_nonempty_block)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (0)
				 .sign (celerix::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	block->signature = celerix::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block.put (transaction, hash1, *block);
	auto latest2 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (*block, *latest2);
}

TEST (block_store, add_two_items)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (1)
				 .sign (celerix::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	block->signature = celerix::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	auto block2 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (3)
				  .sign (celerix::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block2->sideband_set ({});
	block2->hashables.account = 3;
	auto hash2 (block2->hash ());
	block2->signature = celerix::sign_message (key1.prv, key1.pub, hash2);
	auto latest2 (store->block.get (transaction, hash2));
	ASSERT_EQ (nullptr, latest2);
	store->block.put (transaction, hash1, *block);
	store->block.put (transaction, hash2, *block2);
	auto latest3 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (*block, *latest3);
	auto latest4 (store->block.get (transaction, hash2));
	ASSERT_NE (nullptr, latest4);
	ASSERT_EQ (*block2, *latest4);
	ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::keypair key1;
	celerix::keypair key2;
	celerix::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (0)
				  .sign (celerix::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block1->sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, block1->hash (), *block1);
	auto block = builder
				 .receive ()
				 .previous (block1->hash ())
				 .source (1)
				 .sign (celerix::keypair ().prv, 2)
				 .work (3)
				 .build ();
	block->sideband_set ({});
	celerix::block_hash hash1 (block->hash ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block.put (transaction, hash1, *block);
	auto latest2 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (*block, *latest2);
}

TEST (block_store, add_pending)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::keypair key1;
	celerix::pending_key key2 (0, 0);
	auto transaction (store->tx_begin_write ());
	ASSERT_FALSE (store->pending.get (transaction, key2));
	celerix::pending_info pending1;
	store->pending.put (transaction, key2, pending1);
	std::optional<celerix::pending_info> pending2;
	ASSERT_TRUE (pending2 = store->pending.get (transaction, key2));
	ASSERT_EQ (pending1, pending2);
	store->pending.del (transaction, key2);
	ASSERT_FALSE (store->pending.get (transaction, key2));
}

TEST (block_store, pending_iterator)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	ASSERT_EQ (store->pending.end (transaction), store->pending.begin (transaction));
	store->pending.put (transaction, celerix::pending_key (1, 2), { 2, 3, celerix::epoch::epoch_1 });
	auto current (store->pending.begin (transaction));
	ASSERT_NE (store->pending.end (transaction), current);
	celerix::pending_key key1 (current->first);
	ASSERT_EQ (celerix::account (1), key1.account);
	ASSERT_EQ (celerix::block_hash (2), key1.hash);
	celerix::pending_info pending (current->second);
	ASSERT_EQ (celerix::account (2), pending.source);
	ASSERT_EQ (celerix::amount (3), pending.amount);
	ASSERT_EQ (celerix::epoch::epoch_1, pending.epoch);
}

/**
 * Regression test for Issue 1164
 * This reconstructs the situation where a key is larger in pending than the account being iterated in pending_v1, leaving
 * iteration order up to the value, causing undefined behavior.
 * After the bugfix, the value is compared only if the keys are equal.
 */
TEST (block_store, pending_iterator_comparison)
{
	celerix::test::system system;

	auto store = celerix::make_store (system.logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	// Populate pending
	store->pending.put (transaction, celerix::pending_key (celerix::account (3), celerix::block_hash (1)), celerix::pending_info (celerix::account (10), celerix::amount (1), celerix::epoch::epoch_0));
	store->pending.put (transaction, celerix::pending_key (celerix::account (3), celerix::block_hash (4)), celerix::pending_info (celerix::account (10), celerix::amount (0), celerix::epoch::epoch_0));
	// Populate pending_v1
	store->pending.put (transaction, celerix::pending_key (celerix::account (2), celerix::block_hash (2)), celerix::pending_info (celerix::account (10), celerix::amount (2), celerix::epoch::epoch_1));
	store->pending.put (transaction, celerix::pending_key (celerix::account (2), celerix::block_hash (3)), celerix::pending_info (celerix::account (10), celerix::amount (3), celerix::epoch::epoch_1));

	// Iterate account 3 (pending)
	{
		size_t count = 0;
		celerix::account begin (3);
		celerix::account end (begin.number () + 1);
		for (auto i (store->pending.begin (transaction, celerix::pending_key (begin, 0))), n (store->pending.begin (transaction, celerix::pending_key (end, 0))); i != n; ++i, ++count)
		{
			celerix::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}

	// Iterate account 2 (pending_v1)
	{
		size_t count = 0;
		celerix::account begin (2);
		celerix::account end (begin.number () + 1);
		for (auto i (store->pending.begin (transaction, celerix::pending_key (begin, 0))), n (store->pending.begin (transaction, celerix::pending_key (end, 0))); i != n; ++i, ++count)
		{
			celerix::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}
}

TEST (block_store, genesis)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::ledger_cache ledger_cache{ store->rep_weight };
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, ledger_cache, celerix::dev::constants);
	celerix::account_info info;
	ASSERT_FALSE (store->account.get (transaction, celerix::dev::genesis_key.pub, info));
	ASSERT_EQ (celerix::dev::genesis->hash (), info.head);
	auto block1 (store->block.get (transaction, info.head));
	ASSERT_NE (nullptr, block1);
	auto receive1 (dynamic_cast<celerix::open_block *> (block1.get ()));
	ASSERT_NE (nullptr, receive1);
	ASSERT_LE (info.modified, celerix::seconds_since_epoch ());
	ASSERT_EQ (info.block_count, 1);
	// Genesis block should be confirmed by default
	celerix::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height.get (transaction, celerix::dev::genesis_key.pub, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, celerix::dev::genesis->hash ());
	auto dev_pub_text (celerix::dev::genesis_key.pub.to_string ());
	auto dev_pub_account (celerix::dev::genesis_key.pub.to_account ());
	auto dev_prv_text (celerix::dev::genesis_key.prv.to_string ());
	ASSERT_EQ (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.pub);
}

TEST (block_store, empty_accounts)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_read ());
	auto begin (store->account.begin (transaction));
	auto end (store->account.end (transaction));
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (0)
				  .sign (celerix::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block1->sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, block1->hash (), *block1);
	ASSERT_TRUE (store->block.exists (transaction, block1->hash ()));
}

TEST (block_store, empty_bootstrap)
{
	celerix::test::system system{};
	celerix::logger logger;
	unsigned max_unchecked_blocks = 65536;
	celerix::unchecked_map unchecked{ max_unchecked_blocks, system.stats, false };
	size_t count = 0;
	unchecked.for_each ([&count] (celerix::unchecked_key const & key, celerix::unchecked_info const & info) {
		++count;
	});
	ASSERT_EQ (count, 0);
}

TEST (block_store, unchecked_begin_search)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::keypair key0;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (0)
				  .destination (1)
				  .balance (2)
				  .sign (key0.prv, key0.pub)
				  .work (3)
				  .build ();
	auto block2 = builder
				  .send ()
				  .previous (5)
				  .destination (6)
				  .balance (7)
				  .sign (key0.prv, key0.pub)
				  .work (8)
				  .build ();
}

TEST (block_store, frontier_retrieval)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::account account1{};
	celerix::account_info info1 (0, 0, 0, 0, 0, 0, celerix::epoch::epoch_0);
	auto transaction (store->tx_begin_write ());
	store->account.put (transaction, account1, info1);
	celerix::account_info info2;
	store->account.get (transaction, account1, info2);
	ASSERT_EQ (info1, info2);
}

TEST (block_store, one_account)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::account account{};
	celerix::block_hash hash (0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height.put (transaction, account, { 20, celerix::block_hash (15) });
	store->account.put (transaction, account, { hash, account, hash, 42, 100, 200, celerix::epoch::epoch_0 });
	auto begin (store->account.begin (transaction));
	auto end (store->account.end (transaction));
	ASSERT_NE (end, begin);
	ASSERT_EQ (account, celerix::account (begin->first));
	celerix::account_info info (begin->second);
	ASSERT_EQ (hash, info.head);
	ASSERT_EQ (42, info.balance.number ());
	ASSERT_EQ (100, info.modified);
	ASSERT_EQ (200, info.block_count);
	celerix::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height.get (transaction, account, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (celerix::block_hash (15), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (1)
				  .sign (celerix::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block1->sideband_set ({});
	block1->hashables.account = 1;
	std::vector<celerix::block_hash> hashes;
	std::vector<celerix::open_block> blocks;
	hashes.push_back (block1->hash ());
	blocks.push_back (*block1);
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, hashes[0], *block1);
	auto block2 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (2)
				  .sign (celerix::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block2->sideband_set ({});
	hashes.push_back (block2->hash ());
	blocks.push_back (*block2);
	store->block.put (transaction, hashes[1], *block2);
	ASSERT_TRUE (store->block.exists (transaction, block1->hash ()));
	ASSERT_TRUE (store->block.exists (transaction, block2->hash ()));
}

TEST (block_store, two_account)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::account account1 (1);
	celerix::block_hash hash1 (2);
	celerix::account account2 (3);
	celerix::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height.put (transaction, account1, { 20, celerix::block_hash (10) });
	store->account.put (transaction, account1, { hash1, account1, hash1, 42, 100, 300, celerix::epoch::epoch_0 });
	store->confirmation_height.put (transaction, account2, { 30, celerix::block_hash (20) });
	store->account.put (transaction, account2, { hash2, account2, hash2, 84, 200, 400, celerix::epoch::epoch_0 });
	auto begin (store->account.begin (transaction));
	auto end (store->account.end (transaction));
	ASSERT_NE (end, begin);
	ASSERT_EQ (account1, celerix::account (begin->first));
	celerix::account_info info1 (begin->second);
	ASSERT_EQ (hash1, info1.head);
	ASSERT_EQ (42, info1.balance.number ());
	ASSERT_EQ (100, info1.modified);
	ASSERT_EQ (300, info1.block_count);
	celerix::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height.get (transaction, account1, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (celerix::block_hash (10), confirmation_height_info.frontier);
	++begin;
	ASSERT_NE (end, begin);
	ASSERT_EQ (account2, celerix::account (begin->first));
	celerix::account_info info2 (begin->second);
	ASSERT_EQ (hash2, info2.head);
	ASSERT_EQ (84, info2.balance.number ());
	ASSERT_EQ (200, info2.modified);
	ASSERT_EQ (400, info2.block_count);
	ASSERT_FALSE (store->confirmation_height.get (transaction, account2, confirmation_height_info));
	ASSERT_EQ (30, confirmation_height_info.height);
	ASSERT_EQ (celerix::block_hash (20), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::account account1 (1);
	celerix::block_hash hash1 (2);
	celerix::account account2 (3);
	celerix::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	auto first (store->account.begin (transaction));
	auto second (store->account.begin (transaction));
	++second;
	auto find1 (store->account.begin (transaction, 1));
	ASSERT_EQ (first, find1);
	auto find2 (store->account.begin (transaction, 3));
	ASSERT_EQ (second, find2);
	auto find3 (store->account.begin (transaction, 2));
	ASSERT_EQ (second, find3);
}

namespace celerix::store::lmdb
{
TEST (mdb_block_store, supported_version_upgrades)
{
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}

	// Check that upgrading from an unsupported version is not supported
	auto path (celerix::unique_path () / "data.ldb");
	celerix::logger logger;
	{
		celerix::store::lmdb::component store (logger, path, celerix::dev::constants);
		celerix::stats stats{ logger };
		celerix::ledger ledger (store, stats, celerix::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, ledger.cache, celerix::dev::constants);
		// Lower the database to the max version unsupported for upgrades
		store.version.put (transaction, store.version_minimum - 1);
	}

	// Upgrade should fail
	{
		celerix::store::lmdb::component store (logger, path, celerix::dev::constants);
		ASSERT_TRUE (store.init_error ());
	}

	auto path1 (celerix::unique_path () / "data.ldb");
	// Now try with the minimum version
	{
		celerix::store::lmdb::component store (logger, path1, celerix::dev::constants);
		celerix::stats stats{ logger };
		celerix::ledger ledger (store, stats, celerix::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, ledger.cache, celerix::dev::constants);
		// Lower the database version to the minimum version supported for upgrade.
		store.version.put (transaction, store.version_minimum);
	}

	// Upgrade should work
	{
		celerix::store::lmdb::component store (logger, path1, celerix::dev::constants);
		ASSERT_FALSE (store.init_error ());
	}
}
}

TEST (mdb_block_store, bad_path)
{
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	celerix::logger logger;
	try
	{
		auto path = celerix::unique_path ();
		path /= "data.ldb";
		{
			std::ofstream stream (path.c_str ());
		}
		std::filesystem::permissions (path, std::filesystem::perms::none);
		celerix::store::lmdb::component store (logger, path, celerix::dev::constants);
	}
	catch (std::runtime_error &)
	{
		return;
	}
	ASSERT_TRUE (false);
}

TEST (block_store, DISABLED_already_open) // File can be shared
{
	auto path (celerix::unique_path ());
	std::filesystem::create_directories (path.parent_path ());
	celerix::set_secure_perm_directory (path.parent_path ());
	std::ofstream file;
	file.open (path.string ().c_str ());
	ASSERT_TRUE (file.is_open ());
	celerix::logger logger;
	auto store = celerix::make_store (logger, path, celerix::dev::constants);
	ASSERT_TRUE (store->init_error ());
}

TEST (block_store, roots)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::block_builder builder;
	auto send_block = builder
					  .send ()
					  .previous (0)
					  .destination (1)
					  .balance (2)
					  .sign (celerix::keypair ().prv, 4)
					  .work (5)
					  .build ();
	ASSERT_EQ (send_block->hashables.previous, send_block->root ().as_block_hash ());
	auto change_block = builder
						.change ()
						.previous (0)
						.representative (1)
						.sign (celerix::keypair ().prv, 3)
						.work (4)
						.build ();
	ASSERT_EQ (change_block->hashables.previous, change_block->root ().as_block_hash ());
	auto receive_block = builder
						 .receive ()
						 .previous (0)
						 .source (1)
						 .sign (celerix::keypair ().prv, 3)
						 .work (4)
						 .build ();
	ASSERT_EQ (receive_block->hashables.previous, receive_block->root ().as_block_hash ());
	auto open_block = builder
					  .open ()
					  .source (0)
					  .representative (1)
					  .account (2)
					  .sign (celerix::keypair ().prv, 4)
					  .work (5)
					  .build ();
	ASSERT_EQ (open_block->hashables.account, open_block->root ().as_account ());
}

TEST (block_store, pending_exists)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::pending_key two (2, 0);
	celerix::pending_info pending;
	auto transaction (store->tx_begin_write ());
	store->pending.put (transaction, two, pending);
	celerix::pending_key one (1, 0);
	ASSERT_FALSE (store->pending.exists (transaction, one));
}

TEST (block_store, latest_exists)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::account two (2);
	celerix::account_info info;
	auto transaction (store->tx_begin_write ());
	store->account.put (transaction, two, info);
	celerix::account one (1);
	ASSERT_FALSE (store->account.exists (transaction, one));
}

TEST (block_store, large_iteration)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	std::unordered_set<celerix::account> accounts1;
	for (auto i (0); i < 1000; ++i)
	{
		auto transaction (store->tx_begin_write ());
		celerix::account account;
		celerix::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
		accounts1.insert (account);
		store->account.put (transaction, account, celerix::account_info ());
	}
	std::unordered_set<celerix::account> accounts2;
	celerix::account previous{};
	auto transaction (store->tx_begin_read ());
	for (auto i (store->account.begin (transaction, 0)), n (store->account.end (transaction)); i != n; ++i)
	{
		celerix::account current (i->first);
		ASSERT_GT (current.number (), previous.number ());
		accounts2.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts2);
	// Reverse iteration
	std::unordered_set<celerix::account> accounts3;
	previous = std::numeric_limits<celerix::uint256_t>::max ();
	for (auto i (store->account.rbegin (transaction)), n (store->account.rend (transaction)); i != n; ++i)
	{
		celerix::account current (i->first);
		ASSERT_LT (current.number (), previous.number ());
		accounts3.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts3);
}

TEST (block_store, frontier)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	celerix::block_hash hash (100);
	celerix::account account (200);
}

TEST (block_store, block_replace)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (0)
				 .destination (0)
				 .balance (0)
				 .sign (celerix::keypair ().prv, 0)
				 .work (1)
				 .build ();
	send1->sideband_set ({});
	auto send2 = builder
				 .send ()
				 .previous (0)
				 .destination (0)
				 .balance (0)
				 .sign (celerix::keypair ().prv, 0)
				 .work (2)
				 .build ();
	send2->sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, 0, *send1);
	store->block.put (transaction, 0, *send2);
	auto block3 (store->block.get (transaction, 0));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (2, block3->block_work ());
}

TEST (block_store, block_count)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->block.count (transaction));
		celerix::block_builder builder;
		auto block = builder
					 .open ()
					 .source (0)
					 .representative (1)
					 .account (0)
					 .sign (celerix::keypair ().prv, 0)
					 .work (0)
					 .build ();
		block->sideband_set ({});
		auto hash1 (block->hash ());
		store->block.put (transaction, hash1, *block);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->block.count (transaction));
}

TEST (block_store, account_count)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->account.count (transaction));
		celerix::account account (200);
		store->account.put (transaction, account, celerix::account_info ());
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->account.count (transaction));
}

TEST (block_store, cemented_count_cache)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	store->initialize (transaction, ledger.cache, celerix::dev::constants);
	ASSERT_EQ (1, ledger.cemented_count ());
}

TEST (block_store, pruned_random)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (0)
				 .sign (celerix::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	{
		celerix::ledger_cache ledger_cache{ store->rep_weight };
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, ledger_cache, celerix::dev::constants);
		store->pruned.put (transaction, hash1);
	}
	auto transaction (store->tx_begin_read ());
	auto random_hash (store->pruned.random (transaction));
	ASSERT_EQ (hash1, random_hash);
}

TEST (block_store, state_block)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_FALSE (store->init_error ());
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .state ()
				  .account (1)
				  .previous (celerix::dev::genesis->hash ())
				  .representative (3)
				  .balance (4)
				  .link (6)
				  .sign (key1.prv, key1.pub)
				  .work (7)
				  .build ();

	block1->sideband_set ({});
	{
		celerix::ledger_cache ledger_cache{ store->rep_weight };
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, ledger_cache, celerix::dev::constants);
		ASSERT_EQ (celerix::block_type::state, block1->type ());
		store->block.put (transaction, block1->hash (), *block1);
		ASSERT_TRUE (store->block.exists (transaction, block1->hash ()));
		auto block2 (store->block.get (transaction, block1->hash ()));
		ASSERT_NE (nullptr, block2);
		ASSERT_EQ (*block1, *block2);
	}
	{
		auto transaction (store->tx_begin_write ());
		auto count (store->block.count (transaction));
		ASSERT_EQ (2, count);
		store->block.del (transaction, block1->hash ());
		ASSERT_FALSE (store->block.exists (transaction, block1->hash ()));
	}
	auto transaction (store->tx_begin_read ());
	auto count2 (store->block.count (transaction));
	ASSERT_EQ (1, count2);
}

TEST (mdb_block_store, sideband_height)
{
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	celerix::logger logger;
	celerix::keypair key1;
	celerix::keypair key2;
	celerix::keypair key3;
	celerix::store::lmdb::component store (logger, celerix::unique_path () / "data.ldb", celerix::dev::constants);
	ASSERT_FALSE (store.init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (store, stats, celerix::dev::constants);
	celerix::block_builder builder;
	auto transaction = ledger.tx_begin_write ();
	store.initialize (transaction, ledger.cache, celerix::dev::constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto send = builder
				.send ()
				.previous (celerix::dev::genesis->hash ())
				.destination (celerix::dev::genesis_key.pub)
				.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (celerix::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	auto receive = builder
				   .receive ()
				   .previous (send->hash ())
				   .source (send->hash ())
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (send->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive));
	auto change = builder
				  .change ()
				  .previous (receive->hash ())
				  .representative (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (receive->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, change));
	auto state_send1 = builder
					   .state ()
					   .account (celerix::dev::genesis_key.pub)
					   .previous (change->hash ())
					   .representative (0)
					   .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
					   .link (key1.pub)
					   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					   .work (*pool.generate (change->hash ()))
					   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, state_send1));
	auto state_send2 = builder
					   .state ()
					   .account (celerix::dev::genesis_key.pub)
					   .previous (state_send1->hash ())
					   .representative (0)
					   .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
					   .link (key2.pub)
					   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					   .work (*pool.generate (state_send1->hash ()))
					   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, state_send2));
	auto state_send3 = builder
					   .state ()
					   .account (celerix::dev::genesis_key.pub)
					   .previous (state_send2->hash ())
					   .representative (0)
					   .balance (celerix::dev::constants.genesis_amount - 3 * celerix::Kcelerix_ratio)
					   .link (key3.pub)
					   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					   .work (*pool.generate (state_send2->hash ()))
					   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, state_send3));
	auto state_open = builder
					  .state ()
					  .account (key1.pub)
					  .previous (0)
					  .representative (0)
					  .balance (celerix::Kcelerix_ratio)
					  .link (state_send1->hash ())
					  .sign (key1.prv, key1.pub)
					  .work (*pool.generate (key1.pub))
					  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, state_open));
	auto epoch = builder
				 .state ()
				 .account (key1.pub)
				 .previous (state_open->hash ())
				 .representative (0)
				 .balance (celerix::Kcelerix_ratio)
				 .link (ledger.epoch_link (celerix::epoch::epoch_1))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (state_open->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch));
	ASSERT_EQ (celerix::epoch::epoch_1, ledger.version (*epoch));
	auto epoch_open = builder
					  .state ()
					  .account (key2.pub)
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (ledger.epoch_link (celerix::epoch::epoch_1))
					  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					  .work (*pool.generate (key2.pub))
					  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch_open));
	ASSERT_EQ (celerix::epoch::epoch_1, ledger.version (*epoch_open));
	auto state_receive = builder
						 .state ()
						 .account (key2.pub)
						 .previous (epoch_open->hash ())
						 .representative (0)
						 .balance (celerix::Kcelerix_ratio)
						 .link (state_send2->hash ())
						 .sign (key2.prv, key2.pub)
						 .work (*pool.generate (epoch_open->hash ()))
						 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, state_receive));
	auto open = builder
				.open ()
				.source (state_send3->hash ())
				.representative (celerix::dev::genesis_key.pub)
				.account (key3.pub)
				.sign (key3.prv, key3.pub)
				.work (*pool.generate (key3.pub))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open));
	auto block1 = ledger.any.block_get (transaction, celerix::dev::genesis->hash ());
	ASSERT_EQ (block1->sideband ().height, 1);
	auto block2 = ledger.any.block_get (transaction, send->hash ());
	ASSERT_EQ (block2->sideband ().height, 2);
	auto block3 = ledger.any.block_get (transaction, receive->hash ());
	ASSERT_EQ (block3->sideband ().height, 3);
	auto block4 = ledger.any.block_get (transaction, change->hash ());
	ASSERT_EQ (block4->sideband ().height, 4);
	auto block5 = ledger.any.block_get (transaction, state_send1->hash ());
	ASSERT_EQ (block5->sideband ().height, 5);
	auto block6 = ledger.any.block_get (transaction, state_send2->hash ());
	ASSERT_EQ (block6->sideband ().height, 6);
	auto block7 = ledger.any.block_get (transaction, state_send3->hash ());
	ASSERT_EQ (block7->sideband ().height, 7);
	auto block8 = ledger.any.block_get (transaction, state_open->hash ());
	ASSERT_EQ (block8->sideband ().height, 1);
	auto block9 = ledger.any.block_get (transaction, epoch->hash ());
	ASSERT_EQ (block9->sideband ().height, 2);
	auto block10 = ledger.any.block_get (transaction, epoch_open->hash ());
	ASSERT_EQ (block10->sideband ().height, 1);
	auto block11 = ledger.any.block_get (transaction, state_receive->hash ());
	ASSERT_EQ (block11->sideband ().height, 2);
	auto block12 = ledger.any.block_get (transaction, open->hash ());
	ASSERT_EQ (block12->sideband ().height, 1);
}

TEST (block_store, peers)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());

	celerix::endpoint_key endpoint (boost::asio::ip::address_v6::any ().to_bytes (), 100);
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->peer.exists (transaction, endpoint));
		ASSERT_EQ (store->peer.count (transaction), 0);

		// Add one
		store->peer.put (transaction, endpoint, 37);
		ASSERT_TRUE (store->peer.exists (transaction, endpoint));
	}

	// Confirm that it can be found
	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 1);
		ASSERT_EQ (store->peer.get (transaction, endpoint), 37);
	}

	// Add another one and check that it (and the existing one) can be found
	celerix::endpoint_key endpoint1 (boost::asio::ip::address_v6::any ().to_bytes (), 101);
	{
		auto transaction (store->tx_begin_write ());
		store->peer.put (transaction, endpoint1, 42);
		ASSERT_TRUE (store->peer.exists (transaction, endpoint1)); // Check new peer is here
		ASSERT_TRUE (store->peer.exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 2);
		ASSERT_EQ (store->peer.get (transaction, endpoint), 37);
		ASSERT_EQ (store->peer.get (transaction, endpoint1), 42);
	}

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->peer.del (transaction, endpoint1);
		ASSERT_FALSE (store->peer.exists (transaction, endpoint1)); // Confirm it no longer exists
		ASSERT_TRUE (store->peer.exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 1);
	}

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->peer.del (transaction, endpoint);
		ASSERT_FALSE (store->peer.exists (transaction, endpoint));
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 0);
	}
}

TEST (block_store, endpoint_key_byte_order)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"));
	uint16_t port = 100;
	celerix::endpoint_key endpoint_key (address.to_bytes (), port);

	std::vector<uint8_t> bytes;
	{
		celerix::vectorstream stream (bytes);
		celerix::write (stream, endpoint_key);
	}

	// This checks that the endpoint is serialized as expected, with a size
	// of 18 bytes (16 for ipv6 address and 2 for port), both in network byte order.
	ASSERT_EQ (bytes.size (), 18);
	ASSERT_EQ (bytes[10], 0xff);
	ASSERT_EQ (bytes[11], 0xff);
	ASSERT_EQ (bytes[12], 127);
	ASSERT_EQ (bytes[bytes.size () - 2], 0);
	ASSERT_EQ (bytes.back (), 100);

	// Deserialize the same stream bytes
	celerix::bufferstream stream1 (bytes.data (), bytes.size ());
	celerix::endpoint_key endpoint_key1;
	celerix::read (stream1, endpoint_key1);

	// This should be in network bytes order
	ASSERT_EQ (address.to_bytes (), endpoint_key1.address_bytes ());

	// This should be in host byte order
	ASSERT_EQ (port, endpoint_key1.port ());
}

TEST (block_store, online_weight)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_FALSE (store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->online_weight.count (transaction));
		ASSERT_EQ (store->online_weight.end (transaction), store->online_weight.begin (transaction));
		ASSERT_EQ (store->online_weight.rend (transaction), store->online_weight.rbegin (transaction));
		store->online_weight.put (transaction, 1, 2);
		store->online_weight.put (transaction, 3, 4);
	}
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (2, store->online_weight.count (transaction));
		auto item (store->online_weight.begin (transaction));
		ASSERT_NE (store->online_weight.end (transaction), item);
		ASSERT_EQ (1, item->first);
		ASSERT_EQ (2, item->second.number ());
		auto item_last (store->online_weight.rbegin (transaction));
		ASSERT_NE (store->online_weight.rend (transaction), item_last);
		ASSERT_EQ (3, item_last->first);
		ASSERT_EQ (4, item_last->second.number ());
		store->online_weight.del (transaction, 1);
		ASSERT_EQ (1, store->online_weight.count (transaction));
		ASSERT_EQ (*store->online_weight.begin (transaction), *store->online_weight.rbegin (transaction));
		store->online_weight.del (transaction, 3);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (0, store->online_weight.count (transaction));
	ASSERT_EQ (store->online_weight.end (transaction), store->online_weight.begin (transaction));
	ASSERT_EQ (store->online_weight.rend (transaction), store->online_weight.rbegin (transaction));
}

TEST (block_store, pruned_blocks)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());

	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	auto hash1 (block1->hash ());
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->pruned.exists (transaction, hash1));
		ASSERT_EQ (store->pruned.count (transaction), 0);

		// Add one
		store->pruned.put (transaction, hash1);
		ASSERT_TRUE (store->pruned.exists (transaction, hash1));
	}

	// Confirm that it can be found
	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 1);

	// Add another one and check that it (and the existing one) can be found
	auto block2 = builder
				  .open ()
				  .source (1)
				  .representative (2)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	block2->sideband_set ({});
	auto hash2 (block2->hash ());
	{
		auto transaction (store->tx_begin_write ());
		store->pruned.put (transaction, hash2);
		ASSERT_TRUE (store->pruned.exists (transaction, hash2)); // Check new pruned hash is here
		ASSERT_FALSE (store->block.exists (transaction, hash2));
		ASSERT_TRUE (store->pruned.exists (transaction, hash1)); // Check first pruned hash is still here
		ASSERT_FALSE (store->block.exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 2);

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->pruned.del (transaction, hash2);
		ASSERT_FALSE (store->pruned.exists (transaction, hash2)); // Confirm it no longer exists
		ASSERT_FALSE (store->block.exists (transaction, hash2)); // true for block_exists
		store->block.put (transaction, hash2, *block2); // Add corresponding block
		ASSERT_TRUE (store->block.exists (transaction, hash2));
		ASSERT_TRUE (store->pruned.exists (transaction, hash1)); // Check first pruned hash is still here
		ASSERT_FALSE (store->block.exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 1);

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->pruned.del (transaction, hash1);
		ASSERT_FALSE (store->pruned.exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 0);
}

namespace celerix::store::lmdb
{
TEST (mdb_block_store, upgrade_v21_v22)
{
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}

	auto path (celerix::unique_path () / "data.ldb");
	celerix::logger logger;
	celerix::stats stats{ logger };
	auto const check_correct_state = [&] () {
		celerix::store::lmdb::component store (logger, path, celerix::dev::constants);
		auto transaction (store.tx_begin_write ());
		ASSERT_EQ (store.version.get (transaction), store.version_current);
		MDB_dbi unchecked_handle{ 0 };
		ASSERT_EQ (MDB_NOTFOUND, mdb_dbi_open (store.env.tx (transaction), "unchecked", 0, &unchecked_handle));
	};

	// Testing current version doesn't contain the unchecked table
	check_correct_state ();

	// Setting the database to its 21st version state
	{
		celerix::store::lmdb::component store (logger, path, celerix::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.version.put (transaction, 21);
		MDB_dbi unchecked_handle{ 0 };
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &unchecked_handle));
		ASSERT_EQ (store.version.get (transaction), 21);
	}

	// Testing the upgrade code worked
	check_correct_state ();
}

TEST (mdb_block_store, upgrade_v23_v24)
{
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Direct lmdb operations are used to simulate the old ledger format so this test will not work on RocksDB
		GTEST_SKIP ();
	}

	auto path (celerix::unique_path () / "data.ldb");
	celerix::logger logger;
	celerix::stats stats{ logger };
	auto const check_correct_state = [&] () {
		celerix::store::lmdb::component store (logger, path, celerix::dev::constants);
		auto transaction (store.tx_begin_write ());
		ASSERT_EQ (store.version.get (transaction), store.version_current);
		MDB_dbi frontiers_handle{ 0 };
		ASSERT_EQ (MDB_NOTFOUND, mdb_dbi_open (store.env.tx (transaction), "frontiers", 0, &frontiers_handle));
	};

	// Testing current version doesn't contain the frontiers table
	check_correct_state ();

	// Setting the database to its 23st version state
	{
		celerix::store::lmdb::component store (logger, path, celerix::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.version.put (transaction, 23);
		MDB_dbi frontiers_handle{ 0 };
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "frontiers", MDB_CREATE, &frontiers_handle));
		ASSERT_EQ (store.version.get (transaction), 23);
	}

	// Testing the upgrade code worked
	check_correct_state ();
}
}

namespace celerix::store::rocksdb
{
TEST (rocksdb_block_store, upgrade_v21_v22)
{
	if (!celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in LMDB mode
		GTEST_SKIP ();
	}

	auto const path = celerix::unique_path () / "rocksdb";
	celerix::logger logger;
	celerix::stats stats{ logger };
	auto const check_correct_state = [&] () {
		celerix::store::rocksdb::component store (logger, path, celerix::dev::constants);
		auto transaction (store.tx_begin_write ());
		ASSERT_EQ (store.version.get (transaction), store.version_current);
		ASSERT_FALSE (store.column_family_exists ("unchecked"));
	};

	// Testing current version doesn't contain the unchecked table
	check_correct_state ();

	// Setting the database to its 21st version state
	{
		celerix::store::rocksdb::component store (logger, path, celerix::dev::constants);

		// Create a column family for "unchecked"
		::rocksdb::ColumnFamilyOptions new_cf_options;
		::rocksdb::ColumnFamilyHandle * new_cf_handle;
		::rocksdb::Status status = store.db->CreateColumnFamily (new_cf_options, "unchecked", &new_cf_handle);
		store.handles.emplace_back (new_cf_handle);

		// The new column family was created successfully, and 'new_cf_handle' now points to it.
		ASSERT_TRUE (status.ok ());

		// Rollback the database version number.
		auto transaction (store.tx_begin_write ());
		store.version.put (transaction, 21);
		ASSERT_EQ (store.version.get (transaction), 21);
	}

	// Testing the upgrade code worked
	check_correct_state ();
}
}

// Tests that the new rep_weight table gets filled with all
// existing representatives
TEST (mdb_block_store, upgrade_v22_to_v23)
{
	celerix::logger logger;
	auto const path = celerix::unique_path ();
	celerix::account rep_a{ 123 };
	celerix::account rep_b{ 456 };
	// Setting the database to its 22nd version state
	{
		auto store{ celerix::make_store (logger, path, celerix::dev::constants) };
		auto txn{ store->tx_begin_write () };

		// Add three accounts referencing two representatives
		celerix::account_info info1{};
		info1.representative = rep_a;
		info1.balance = 1000;
		store->account.put (txn, 1, info1);

		celerix::account_info info2{};
		info2.representative = rep_a;
		info2.balance = 500;
		store->account.put (txn, 2, info2);

		celerix::account_info info3{};
		info3.representative = rep_b;
		info3.balance = 42;
		store->account.put (txn, 3, info3);

		store->version.put (txn, 22);
	}

	// Testing the upgrade code worked
	auto store{ celerix::make_store (logger, path, celerix::dev::constants) };
	auto txn (store->tx_begin_read ());
	ASSERT_EQ (store->version.get (txn), store->version_current);

	// The rep_weight table should contain all reps now
	ASSERT_EQ (1500, store->rep_weight.get (txn, rep_a));
	ASSERT_EQ (42, store->rep_weight.get (txn, rep_b));
}

TEST (mdb_block_store, upgrade_backup)
{
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	auto dir (celerix::unique_path ());
	namespace fs = std::filesystem;
	fs::create_directory (dir);
	auto path = dir / "data.ldb";
	/** Returns 'dir' if backup file cannot be found */
	auto get_backup_path = [&dir] () {
		for (fs::directory_iterator itr (dir); itr != fs::directory_iterator (); ++itr)
		{
			if (itr->path ().filename ().string ().find ("data_backup_") != std::string::npos)
			{
				return itr->path ();
			}
		}
		return dir;
	};

	{
		celerix::logger logger;
		celerix::store::lmdb::component store (logger, path, celerix::dev::constants);
		auto transaction (store.tx_begin_write ());
		store.version.put (transaction, store.version_minimum);
	}
	ASSERT_EQ (get_backup_path ().string (), dir.string ());

	// Now do the upgrade and confirm that backup is saved
	celerix::logger logger;
	celerix::store::lmdb::component store (logger, path, celerix::dev::constants, celerix::txn_tracking_config{}, std::chrono::seconds (5), celerix::lmdb_config{}, true);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (14, store.version.get (transaction));
	ASSERT_NE (get_backup_path ().string (), dir.string ());
}

// Test various confirmation height values as well as clearing them
TEST (block_store, confirmation_height)
{
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	auto path (celerix::unique_path ());
	celerix::logger logger;
	auto store = celerix::make_store (logger, path, celerix::dev::constants);

	celerix::account account1{};
	celerix::account account2{ 1 };
	celerix::account account3{ 2 };
	celerix::block_hash cemented_frontier1 (3);
	celerix::block_hash cemented_frontier2 (4);
	celerix::block_hash cemented_frontier3 (5);
	{
		auto transaction (store->tx_begin_write ());
		store->confirmation_height.put (transaction, account1, { 500, cemented_frontier1 });
		store->confirmation_height.put (transaction, account2, { std::numeric_limits<uint64_t>::max (), cemented_frontier2 });
		store->confirmation_height.put (transaction, account3, { 10, cemented_frontier3 });

		celerix::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store->confirmation_height.get (transaction, account1, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 500);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier1);
		ASSERT_FALSE (store->confirmation_height.get (transaction, account2, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, std::numeric_limits<uint64_t>::max ());
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier2);
		ASSERT_FALSE (store->confirmation_height.get (transaction, account3, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 10);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier3);

		// Check clearing of confirmation heights
		store->confirmation_height.clear (transaction);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (store->confirmation_height.count (transaction), 0);
	celerix::confirmation_height_info confirmation_height_info;
	ASSERT_TRUE (store->confirmation_height.get (transaction, account1, confirmation_height_info));
	ASSERT_TRUE (store->confirmation_height.get (transaction, account2, confirmation_height_info));
	ASSERT_TRUE (store->confirmation_height.get (transaction, account3, confirmation_height_info));
}

// Test various confirmation height values as well as clearing them
TEST (block_store, final_vote)
{
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode as deletions cause inaccurate counts
		GTEST_SKIP ();
	}
	auto path (celerix::unique_path ());
	celerix::logger logger;
	auto store = celerix::make_store (logger, path, celerix::dev::constants);

	{
		auto qualified_root = celerix::dev::genesis->qualified_root ();
		auto transaction (store->tx_begin_write ());
		store->final_vote.put (transaction, qualified_root, celerix::block_hash (2));
		ASSERT_EQ (store->final_vote.count (transaction), 1);
		store->final_vote.clear (transaction);
		ASSERT_EQ (store->final_vote.count (transaction), 0);
		store->final_vote.put (transaction, qualified_root, celerix::block_hash (2));
		ASSERT_EQ (store->final_vote.count (transaction), 1);
		// Clearing with correct root should remove
		store->final_vote.del (transaction, qualified_root);
		ASSERT_EQ (store->final_vote.count (transaction), 0);
	}
}

// Ledger versions are not forward compatible
TEST (block_store, incompatible_version)
{
	auto path (celerix::unique_path ());
	celerix::logger logger;
	{
		auto store = celerix::make_store (logger, path, celerix::dev::constants);
		ASSERT_FALSE (store->init_error ());

		// Put version to an unreachable number so that it should always be incompatible
		auto transaction (store->tx_begin_write ());
		store->version.put (transaction, std::numeric_limits<int>::max ());
	}

	// Now try and read it, should give an error
	{
		auto store = celerix::make_store (logger, path, celerix::dev::constants, true);
		ASSERT_TRUE (store->init_error ());

		auto transaction = store->tx_begin_read ();
		auto version_l = store->version.get (transaction);
		ASSERT_EQ (version_l, std::numeric_limits<int>::max ());
	}
}

TEST (block_store, reset_renew_existing_transaction)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());

	celerix::keypair key1;
	celerix::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (1)
				 .sign (celerix::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	auto read_transaction = store->tx_begin_read ();

	// Block shouldn't exist yet
	auto block_non_existing (store->block.get (read_transaction, hash1));
	ASSERT_EQ (nullptr, block_non_existing);

	// Release resources for the transaction
	read_transaction.reset ();

	// Write the block
	{
		auto write_transaction (store->tx_begin_write ());
		store->block.put (write_transaction, hash1, *block);
	}

	read_transaction.renew ();

	// Block should exist now
	auto block_existing (store->block.get (read_transaction, hash1));
	ASSERT_NE (nullptr, block_existing);
}

TEST (block_store, rocksdb_force_test_env_variable)
{
	celerix::logger logger;

	// Set environment variable
	constexpr auto env_var = "TEST_USE_ROCKSDB";
	auto value = std::getenv (env_var);

	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);

	auto mdb_cast = dynamic_cast<celerix::store::lmdb::component *> (store.get ());
	if (value && boost::lexical_cast<int> (value) == 1)
	{
		ASSERT_NE (boost::polymorphic_downcast<celerix::store::rocksdb::component *> (store.get ()), nullptr);
	}
	else
	{
		ASSERT_NE (mdb_cast, nullptr);
	}
}

namespace celerix
{
// This thest ensures the tombstone_count is increased when there is a delete. The tombstone_count is part of a flush
// logic bound to the way RocksDB is used by the node.
TEST (rocksdb_block_store, tombstone_count)
{
	if (!celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		GTEST_SKIP ();
	}
	celerix::test::system system;
	celerix::logger logger;
	auto store = std::make_unique<celerix::store::rocksdb::component> (logger, celerix::unique_path () / "rocksdb", celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (0)
				 .destination (1)
				 .balance (2)
				 .sign (celerix::keypair ().prv, 4)
				 .work (5)
				 .build ();
	// Enqueues a block to be saved in the database
	celerix::account account{ 1 };
	store->account.put (store->tx_begin_write (), account, celerix::account_info{});
	auto check_block_is_listed = [&] (store::transaction const & transaction_a) {
		return store->account.exists (transaction_a, account);
	};
	// Waits for the block to get saved
	ASSERT_TIMELY (5s, check_block_is_listed (store->tx_begin_read ()));
	ASSERT_EQ (store->tombstone_map.at (celerix::tables::accounts).num_since_last_flush.load (), 0);
	// Performs a delete operation and checks for the tombstone counter
	store->account.del (store->tx_begin_write (), account);
	ASSERT_TIMELY_EQ (5s, store->tombstone_map.at (celerix::tables::accounts).num_since_last_flush.load (), 1);
}
}
