#include <gtest/gtest.h>
#include <rai/node/node.hpp>
#include <rai/node/testing.hpp>

static boost::log::sources::logger_mt test_log;

class test_synchronization : public rai::block_synchronization
{
public:
	test_synchronization (rai::block_store & store_a) :
	rai::block_synchronization (test_log),
	store (store_a)
	{
	}
    bool synchronized (MDB_txn * transaction_a, rai::block_hash const & hash_a) override
	{
		return store.block_exists (transaction_a, hash_a);
	}
    std::unique_ptr <rai::block> retrieve (MDB_txn * transaction_a, rai::block_hash const & hash_a) override
	{
		return store.unchecked_get (transaction_a, hash_a);
	}
    rai::sync_result target (MDB_txn * transaction_a, rai::block const & block_a) override
	{
		store.block_put (transaction_a, block_a.hash (), block_a);
		return rai::sync_result::success;
	}
	rai::block_store & store;
};

TEST (pull_synchronization, empty)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	test_synchronization sync (store);
	rai::transaction transaction (store.environment, nullptr, true);
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, 0));
	ASSERT_EQ (0, store.block_count (transaction).sum ());
}

TEST (pull_synchronization, one)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::transaction transaction (store.environment, nullptr, true);
	store.block_put (transaction, block1.hash (), block1);
	store.unchecked_put (transaction, block2.hash (), block2);
	test_synchronization sync (store);
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block2.hash ()));
	ASSERT_EQ (2, store.block_count (transaction).sum ());
	ASSERT_NE (nullptr, store.block_get (transaction, block2.hash ()));
}

TEST (pull_synchronization, send_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::send_block block3 (block2.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::transaction transaction (store.environment, nullptr, true);
	store.block_put (transaction, block1.hash (), block1);
	store.unchecked_put (transaction, block2.hash (), block2);
	store.unchecked_put (transaction, block3.hash (), block3);
	test_synchronization sync (store);
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block3.hash ()));
	ASSERT_EQ (3, store.block_count (transaction).sum ());
	ASSERT_NE (nullptr, store.block_get (transaction, block2.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block3.hash ()));
}

TEST (pull_synchronization, change_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::change_block block3 (block2.hash (), 0, rai::keypair ().prv, 2, 3);
	rai::transaction transaction (store.environment, nullptr, true);
	store.block_put (transaction, block1.hash (), block1);
	store.unchecked_put (transaction, block2.hash (), block2);
	store.unchecked_put (transaction, block3.hash (), block3);
	test_synchronization sync (store);
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block3.hash ()));
	ASSERT_EQ (3, store.block_count (transaction).sum ());
	ASSERT_NE (nullptr, store.block_get (transaction, block2.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block3.hash ()));
}

TEST (pull_synchronization, open_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::open_block block3 (block2.hash (), 1, 1, rai::keypair ().prv, 4, 5);
	rai::transaction transaction (store.environment, nullptr, true);
	store.block_put (transaction, block1.hash (), block1);
	store.unchecked_put (transaction, block2.hash (), block2);
	store.unchecked_put (transaction, block3.hash (), block3);
	test_synchronization sync (store);
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block3.hash ()));
	ASSERT_EQ (3, store.block_count (transaction).sum ());
	ASSERT_NE (nullptr, store.block_get (transaction, block2.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block3.hash ()));
}

TEST (pull_synchronization, receive_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::open_block block3 (block2.hash (), 1, 1, rai::keypair ().prv, 4, 5);
	rai::send_block block4 (block2.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::receive_block block5 (block3.hash (), block4.hash (), rai::keypair ().prv, 0, 0);
	rai::transaction transaction (store.environment, nullptr, true);
	store.block_put (transaction, block1.hash (), block1);
	store.unchecked_put (transaction, block2.hash (), block2);
	store.unchecked_put (transaction, block3.hash (), block3);
	store.unchecked_put (transaction, block4.hash (), block4);
	store.unchecked_put (transaction, block5.hash (), block5);
	test_synchronization sync (store);
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block5.hash ()));
	ASSERT_EQ (3, store.block_count (transaction).sum ());
	// Synchronize 2 per iteration in test mode
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block5.hash ()));
	ASSERT_EQ (5, store.block_count (transaction).sum ());
	ASSERT_NE (nullptr, store.block_get (transaction, block2.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block3.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block4.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block5.hash ()));
}

TEST (pull_synchronization, ladder_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::open_block block3 (block2.hash (), 1, 1, rai::keypair ().prv, 4, 5);
	rai::send_block block4 (block3.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::receive_block block5 (block2.hash (), block4.hash (), rai::keypair ().prv, 0, 0);
	rai::send_block block6 (block5.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::receive_block block7 (block4.hash (), block6.hash (), rai::keypair ().prv, 0, 0);
	rai::transaction transaction (store.environment, nullptr, true);
	store.block_put (transaction, block1.hash (), block1);
	store.unchecked_put (transaction, block2.hash (), block2);
	store.unchecked_put (transaction, block3.hash (), block3);
	store.unchecked_put (transaction, block4.hash (), block4);
	store.unchecked_put (transaction, block5.hash (), block5);
	store.unchecked_put (transaction, block6.hash (), block6);
	store.unchecked_put (transaction, block7.hash (), block7);
	test_synchronization sync (store);
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block7.hash ()));
	ASSERT_EQ (3, store.block_count (transaction).sum ());
	// Synchronize 2 per iteration in test mode
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block7.hash ()));
	// Synchronize 2 per iteration in test mode
	ASSERT_EQ (5, store.block_count (transaction).sum ());
	ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block7.hash ()));
	ASSERT_EQ (7, store.block_count (transaction).sum ());
	ASSERT_NE (nullptr, store.block_get (transaction, block2.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block3.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block4.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block5.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block6.hash ()));
	ASSERT_NE (nullptr, store.block_get (transaction, block7.hash ()));
}

/*TEST (push_synchronization, empty)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	std::vector <std::unique_ptr <rai::block>> blocks;
	rai::push_synchronization sync (test_log, [&blocks] (MDB_txn * transaction_a, rai::block const & block_a)
	{
		blocks.push_back (block_a.clone ());
		return rai::sync_result::success;
	});
	{
		rai::transaction transaction (store.environment, nullptr, true);
		ASSERT_EQ (rai::sync_result::error, sync.synchronize (transaction, 0));
	}
	ASSERT_EQ (0, blocks.size ());
}

TEST (push_synchronization, one)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.block_put (transaction, block2.hash (), block2);
	}
	rai::push_synchronization sync (test_log, [&blocks, &store] (MDB_txn * transaction_a, rai::block const & block_a)
	{
		store.block_put (transaction_a, block_a.hash (), block_a);
		blocks.push_back (block_a.clone ());
		return rai::sync_result::success;
	}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.unsynced_put (transaction, block2.hash ());
		ASSERT_EQ (rai::sync_result::success, sync.synchronize (transaction, block2.hash ()));
	}
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
}*/

// Make sure synchronize terminates even with forks
TEST (pull_synchronization, dependent_fork)
{
	rai::system system0 (24000, 1);
	auto & node0 (*system0.nodes [0]);
	rai::keypair key0;
	rai::keypair key1;
	rai::transaction transaction (node0.store.environment, nullptr, true);
	auto genesis (node0.ledger.latest (transaction, rai::test_genesis_key.pub));
	rai::send_block send0 (genesis, key0.pub, rai::genesis_amount - 1 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::send_block send1 (genesis, key0.pub, rai::genesis_amount - 2 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::send_block send2 (send0.hash (), key0.pub, rai::genesis_amount - 3 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::send_block send3 (send1.hash (), key0.pub, rai::genesis_amount - 4 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, node0.ledger.process (transaction, send0).code);
	ASSERT_EQ (rai::process_result::progress, node0.ledger.process (transaction, send2).code);
	node0.store.unchecked_put (transaction, send1.hash (), send1);
	node0.store.unchecked_put (transaction, send3.hash (), send3);
	rai::pull_synchronization sync (node0, nullptr);
	ASSERT_EQ (rai::sync_result::fork, sync.synchronize (transaction, send3.hash ()));
	// Voting will either discard this block or commit it.  If it's discarded we don't want to attempt it again
	ASSERT_EQ (nullptr, node0.store.unchecked_get (transaction, send1.hash ()));
	// This block will either succeed, if its predecessor is comitted by voting, or will be a gap and will be discarded
	ASSERT_NE (nullptr, node0.store.unchecked_get (transaction, send3.hash ()));
	ASSERT_TRUE (node0.active.active (send1));
}

// Make sure that when synchronizing, if a fork needs to be resolved, don't drop the blocks we downloaded.
TEST (pull_synchronization, keep_blocks)
{
	rai::system system0 (24000, 1);
	rai::system system1 (24001, 1);
	auto & node0 (*system0.nodes [0]);
	auto & node1 (*system1.nodes [0]);
	system0.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::block_hash block0;
	rai::block_hash block1;
	{
		rai::keypair key0;
		rai::keypair key1;
		rai::transaction transaction0 (node0.store.environment, nullptr, true);
		rai::transaction transaction1 (node1.store.environment, nullptr, true);
		auto genesis (node0.ledger.latest (transaction0, rai::genesis_account));
		// This is the first block to be kept
		rai::send_block send0 (genesis, key0.pub, rai::genesis_amount - 1 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		rai::send_block send1 (genesis, key0.pub, rai::genesis_amount - 2 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		// This is the second block to be kept
		rai::send_block send2 (send0.hash (), key0.pub, rai::genesis_amount - 4 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		rai::send_block send3 (send1.hash (), key0.pub, rai::genesis_amount - 6 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		node0.generate_work (send0);
		node0.generate_work (send1);
		node0.generate_work (send2);
		node0.generate_work (send3);
		ASSERT_EQ (rai::process_result::progress, node0.ledger.process (transaction0, send0).code);
		ASSERT_EQ (rai::process_result::progress, node0.ledger.process (transaction0, send2).code);
		ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction1, send1).code);
		ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction1, send3).code);
		block0 = send0.hash ();
		block1 = send2.hash ();
	}
	node0.bootstrap_initiator.stop ();
	node1.send_keepalive (node0.network.endpoint ());
	node1.bootstrap_initiator.bootstrap (node0.network.endpoint ());
	auto state (0);
	auto iterations (0);
	// Node1 starts out with a different balance and should end up the same, both on what node0 has.
	ASSERT_EQ (rai::genesis_amount - 6 * rai::Grai_ratio, node1.balance (rai::genesis_account));
	while (node0.balance (rai::genesis_account) != node1.balance (rai::genesis_account))
	{
		// Node0 should never chaneg its mind since it has the genesis rep
		ASSERT_EQ (rai::genesis_amount - 4 * rai::Grai_ratio, node0.balance (rai::genesis_account));
		{
			rai::transaction transaction (node1.store.environment, nullptr, false);
			auto have0 (node1.store.unchecked_get (transaction, block0) != nullptr);
			auto have1 (node1.store.unchecked_get (transaction, block1) != nullptr);
			switch (state)
			{
				case 0:
					// Bootstrap hasn't started yet
					if (!have0 && !have1)
					{
						state = 1;
					}
					break;
				case 1:
					// Bootstrapping stopped but block1 was kept, in case block0 was chosen we don't want to re-download it.
					if (!have0 && have1)
					{
						state = 2;
					}
					break;
			}
		}
		++iterations;
		ASSERT_GT (200, iterations);
		system0.poll ();
		system1.poll ();
	}
}

// After a synchronization with no pulls or pushes required, clear blocks out of unchecked
TEST (pull_synchronization, clear_blocks)
{
	rai::system system0 (24000, 1);
	rai::system system1 (24001, 1);
	auto & node0 (*system0.nodes [0]);
	auto & node1 (*system1.nodes [0]);
	rai::send_block send0 (0, 0, rai::genesis_amount - 1 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	{
		rai::transaction transaction (node1.store.environment, nullptr, true);
		node1.store.unchecked_put (transaction, send0.hash (), send0);
	}
	node1.bootstrap_initiator.bootstrap (node0.network.endpoint ());
	auto iterations (0);
	auto done (false);
	while (!done)
	{
		{
			rai::transaction transaction (node1.store.environment, nullptr, false);
			done = node1.store.unchecked_get (transaction, send0.hash ()) == nullptr;
		}
		++iterations;
		ASSERT_GT (200, iterations);
		system0.poll ();
		system1.poll ();
	}
}
