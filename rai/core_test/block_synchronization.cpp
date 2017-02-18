#include <gtest/gtest.h>
#include <rai/node/node.hpp>
#include <rai/node/testing.hpp>

static boost::log::sources::logger_mt test_log;

TEST (pull_synchronization, empty)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	std::vector <std::unique_ptr <rai::block>> blocks;
	rai::pull_synchronization sync (test_log, [&blocks] (rai::transaction &, rai::block const & block_a)
	{
		blocks.push_back (block_a.clone ());
	}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		ASSERT_TRUE (sync.synchronize (transaction, 0));
	}
	ASSERT_EQ (0, blocks.size ());
}

TEST (pull_synchronization, one)
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
		store.unchecked_put (transaction, block2.hash (), block2);
	}
	rai::pull_synchronization sync (test_log, [&blocks] (rai::transaction &, rai::block const & block_a)
	{
		blocks.push_back (block_a.clone ());
	}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		ASSERT_FALSE (sync.synchronize (transaction, block2.hash ()));
	}
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
}

TEST (pull_synchronization, send_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::send_block block3 (block2.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
		store.unchecked_put (transaction, block3.hash (), block3);
	}
	rai::pull_synchronization sync (test_log, [&blocks, &store] (rai::transaction & transaction_a, rai::block const & block_a)
									{
										store.block_put (transaction_a, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	rai::transaction transaction (store.environment, nullptr, true);
	ASSERT_FALSE (sync.synchronize (transaction, block3.hash ()));
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
	ASSERT_EQ (block3, *blocks [1]);
}

TEST (pull_synchronization, change_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::change_block block3 (block2.hash (), 0, rai::keypair ().prv, 2, 3);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
		store.unchecked_put (transaction, block3.hash (), block3);
	}
	rai::pull_synchronization sync (test_log, [&blocks, &store] (rai::transaction & transaction_a, rai::block const & block_a)
									{
										store.block_put (transaction_a, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		ASSERT_FALSE (sync.synchronize (transaction, block3.hash ()));
	}
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
	ASSERT_EQ (block3, *blocks [1]);
}

TEST (pull_synchronization, open_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, rai::keypair ().prv, 4, 5);
	rai::send_block block2 (block1.hash (), 0, 1, rai::keypair ().prv, 3, 4);
	rai::open_block block3 (block2.hash (), 1, 1, rai::keypair ().prv, 4, 5);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
		store.unchecked_put (transaction, block3.hash (), block3);
	}
	rai::pull_synchronization sync (test_log, [&blocks, &store] (rai::transaction & transaction_a, rai::block const & block_a)
									{
										store.block_put (transaction_a, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		ASSERT_FALSE (sync.synchronize (transaction, block3.hash ()));
	}
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
	ASSERT_EQ (block3, *blocks [1]);
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
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
		store.unchecked_put (transaction, block3.hash (), block3);
		store.unchecked_put (transaction, block4.hash (), block4);
		store.unchecked_put (transaction, block5.hash (), block5);
	}
	rai::pull_synchronization sync (test_log, [&blocks, &store] (rai::transaction & transaction_a, rai::block const & block_a)
									{
										store.block_put (transaction_a, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		ASSERT_FALSE (sync.synchronize (transaction, block5.hash ()));
	}
	ASSERT_EQ (4, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
	ASSERT_EQ (block3, *blocks [1]);
	ASSERT_EQ (block4, *blocks [2]);
	ASSERT_EQ (block5, *blocks [3]);
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
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
		store.unchecked_put (transaction, block3.hash (), block3);
		store.unchecked_put (transaction, block4.hash (), block4);
		store.unchecked_put (transaction, block5.hash (), block5);
		store.unchecked_put (transaction, block6.hash (), block6);
		store.unchecked_put (transaction, block7.hash (), block7);
	}
	rai::pull_synchronization sync (test_log, [&blocks, &store] (rai::transaction & transaction_a, rai::block const & block_a)
									{
										store.block_put (transaction_a, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		ASSERT_FALSE (sync.synchronize (transaction, block7.hash ()));
	}
	ASSERT_EQ (6, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
	ASSERT_EQ (block3, *blocks [1]);
	ASSERT_EQ (block4, *blocks [2]);
	ASSERT_EQ (block5, *blocks [3]);
	ASSERT_EQ (block6, *blocks [4]);
	ASSERT_EQ (block7, *blocks [5]);
}

TEST (push_synchronization, empty)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	std::vector <std::unique_ptr <rai::block>> blocks;
	rai::push_synchronization sync (test_log, [&blocks] (rai::transaction & transaction_a, rai::block const & block_a)
	{
		blocks.push_back (block_a.clone ());
	}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		ASSERT_TRUE (sync.synchronize (transaction, 0));
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
	rai::push_synchronization sync (test_log, [&blocks, &store] (rai::transaction & transaction_a, rai::block const & block_a)
									{
										store.block_put (transaction_a, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.unsynced_put (transaction, block2.hash ());
		ASSERT_FALSE (sync.synchronize (transaction, block2.hash ()));
	}
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
}

// Make sure that when synchronizing, if a fork needs to be resolved, don't drop the blocks we downloaded.
TEST (pull_synchronization, DISABLED_keep_blocks)
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
		rai::send_block send0 (genesis, key0.pub, rai::genesis_amount - 1 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		rai::send_block send1 (genesis, key0.pub, rai::genesis_amount - 2 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		rai::send_block send2 (send0.hash (), key0.pub, rai::genesis_amount - 3 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		rai::send_block send3 (send1.hash (), key0.pub, rai::genesis_amount - 4 * rai::Grai_ratio, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		node0.generate_work (send0);
		node0.generate_work (send1);
		node0.generate_work (send2);
		node0.generate_work (send3);
		node0.ledger.process (transaction0, send0);
		node0.ledger.process (transaction0, send2);
		node0.ledger.process (transaction1, send1);
		node0.ledger.process (transaction1, send3);
		block0 = send0.hash ();
		block1 = send1.hash ();
	}
	node1.send_keepalive (node0.network.endpoint ());
	node1.bootstrap_initiator.bootstrap (node0.network.endpoint ());
	auto state (0);
	auto iterations (0);
	while (node0.balance (rai::genesis_account) != node1.balance (rai::genesis_account))
	{
		{
			rai::transaction transaction (node1.store.environment, nullptr, false);
			switch (state)
			{
				case 0:
					if (node0.store.unchecked_get (transaction, block0) == nullptr && node0.store.unchecked_get (transaction, block1) == nullptr)
					state = 1;
					break;
				case 1:
					if (node0.store.unchecked_get (transaction, block0) != nullptr && node0.store.unchecked_get (transaction, block1) != nullptr)
					state = 2;
					break;
				case 2:
					if (node0.store.unchecked_get (transaction, block0) == nullptr && node0.store.unchecked_get (transaction, block1) == nullptr)
					state = 3;
					break;
				case 3:
					ASSERT_FALSE (node0.store.unchecked_get (transaction, block0) != nullptr || node0.store.unchecked_get (transaction, block1) != nullptr);
					break;
			}
		}
		++iterations;
		ASSERT_GT (200, iterations);
		system0.poll ();
		system1.poll ();
	}
	
}
