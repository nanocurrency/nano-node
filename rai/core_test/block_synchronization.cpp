#include <gtest/gtest.h>
#include <rai/node.hpp>

TEST (pull_synchronization, empty)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	std::vector <std::unique_ptr <rai::block>> blocks;
	rai::pull_synchronization sync ([&blocks] (rai::block const & block_a)
	{
		blocks.push_back (block_a.clone ());
	}, store);
	ASSERT_TRUE (sync.synchronize (0));
	ASSERT_EQ (0, blocks.size ());
}

TEST (pull_synchronization, one)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, 3, 4, 5);
	rai::send_block block2 (0, block1.hash (), 1, 2, 3, 4);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
	}
	rai::pull_synchronization sync ([&blocks] (rai::block const & block_a)
	{
		blocks.push_back (block_a.clone ());
	}, store);
	ASSERT_FALSE (sync.synchronize (block2.hash ()));
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
}

TEST (pull_synchronization, send_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, 3, 4, 5);
	rai::send_block block2 (0, block1.hash (), 1, 2, 3, 4);
	rai::send_block block3 (0, block2.hash (), 1, 2, 3, 4);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
		store.unchecked_put (transaction, block3.hash (), block3);
	}
	rai::pull_synchronization sync ([&blocks, &store] (rai::block const & block_a)
									{
										rai::transaction transaction (store.environment, nullptr, true);
										store.block_put (transaction, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	ASSERT_FALSE (sync.synchronize (block3.hash ()));
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
	ASSERT_EQ (block3, *blocks [1]);
}

TEST (pull_synchronization, change_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, 3, 4, 5);
	rai::send_block block2 (0, block1.hash (), 1, 2, 3, 4);
	rai::change_block block3 (0, block2.hash (), 1, 2, 3);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
		store.unchecked_put (transaction, block3.hash (), block3);
	}
	rai::pull_synchronization sync ([&blocks, &store] (rai::block const & block_a)
									{
										rai::transaction transaction (store.environment, nullptr, true);
										store.block_put (transaction, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	ASSERT_FALSE (sync.synchronize (block3.hash ()));
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
	ASSERT_EQ (block3, *blocks [1]);
}

TEST (pull_synchronization, open_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, 3, 4, 5);
	rai::send_block block2 (0, block1.hash (), 1, 2, 3, 4);
	rai::open_block block3 (0, 1, block2.hash (), 3, 4, 5);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
		store.unchecked_put (transaction, block3.hash (), block3);
	}
	rai::pull_synchronization sync ([&blocks, &store] (rai::block const & block_a)
									{
										rai::transaction transaction (store.environment, nullptr, true);
										store.block_put (transaction, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	ASSERT_FALSE (sync.synchronize (block3.hash ()));
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
	ASSERT_EQ (block3, *blocks [1]);
}

TEST (pull_synchronization, receive_dependencies)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, 3, 4, 5);
	rai::send_block block2 (0, block1.hash (), 1, 2, 3, 4);
	rai::open_block block3 (0, 1, block2.hash (), 3, 4, 5);
	rai::send_block block4 (0, block2.hash (), 1, 2, 3, 4);
	rai::receive_block block5 (block3.hash (), block4.hash (), 0, 0, 0);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.unchecked_put (transaction, block2.hash (), block2);
		store.unchecked_put (transaction, block3.hash (), block3);
		store.unchecked_put (transaction, block4.hash (), block4);
		store.unchecked_put (transaction, block5.hash (), block5);
	}
	rai::pull_synchronization sync ([&blocks, &store] (rai::block const & block_a)
									{
										rai::transaction transaction (store.environment, nullptr, true);
										store.block_put (transaction, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	ASSERT_FALSE (sync.synchronize (block5.hash ()));
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
	rai::open_block block1 (0, 1, 2, 3, 4, 5);
	rai::send_block block2 (0, block1.hash (), 1, 2, 3, 4);
	rai::open_block block3 (0, 1, block2.hash (), 3, 4, 5);
	rai::send_block block4 (0, block3.hash (), 1, 2, 3, 4);
	rai::receive_block block5 (block2.hash (), block4.hash (), 0, 0, 0);
	rai::send_block block6 (0, block5.hash (), 1, 2, 3, 4);
	rai::receive_block block7 (block4.hash (), block6.hash (), 0, 0, 0);
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
	rai::pull_synchronization sync ([&blocks, &store] (rai::block const & block_a)
									{
										rai::transaction transaction (store.environment, nullptr, true);
										store.block_put (transaction, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	ASSERT_FALSE (sync.synchronize (block7.hash ()));
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
	rai::push_synchronization sync ([&blocks] (rai::block const & block_a)
	{
		blocks.push_back (block_a.clone ());
	}, store);
	ASSERT_TRUE (sync.synchronize (0));
	ASSERT_EQ (0, blocks.size ());
}

TEST (push_synchronization, one)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::open_block block1 (0, 1, 2, 3, 4, 5);
	rai::send_block block2 (0, block1.hash (), 1, 2, 3, 4);
	std::vector <std::unique_ptr <rai::block>> blocks;
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.block_put (transaction, block1.hash (), block1);
		store.block_put (transaction, block2.hash (), block2);
	}
	rai::push_synchronization sync ([&blocks, &store] (rai::block const & block_a)
									{
										rai::transaction transaction (store.environment, nullptr, true);
										store.block_put (transaction, block_a.hash (), block_a);
										blocks.push_back (block_a.clone ());
									}, store);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		store.unsynced_put (transaction, block2.hash ());
	}
	ASSERT_FALSE (sync.synchronize (block2.hash ()));
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block2, *blocks [0]);
}