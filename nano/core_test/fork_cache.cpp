#include <nano/node/fork_cache.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <map>

TEST (fork_cache, construction)
{
	nano::test::system system;
	nano::fork_cache_config cfg;
	nano::fork_cache fork_cache{ cfg, system.stats };
	ASSERT_EQ (0, fork_cache.size ());
	ASSERT_FALSE (fork_cache.contains (nano::qualified_root{}));
}

/*
 * Inserts a single block to cache, ensures it can be retrieved
 */
TEST (fork_cache, one)
{
	nano::test::system system;
	nano::fork_cache_config cfg;
	nano::fork_cache fork_cache{ cfg, system.stats };

	auto block = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::dev::genesis->hash (), nano::dev::genesis_key.pub, nano::Knano_ratio, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);
	nano::qualified_root root = block->qualified_root ();

	fork_cache.put (block);
	ASSERT_EQ (1, fork_cache.size ());
	ASSERT_TRUE (fork_cache.contains (root));

	auto blocks = fork_cache.get (root);
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block, blocks.front ());
}

/*
 * Inserts multiple blocks with same root, ensures all are retrievable
 */
TEST (fork_cache, multiple_forks)
{
	nano::test::system system;
	nano::fork_cache_config cfg;
	nano::fork_cache fork_cache{ cfg, system.stats };

	// Create several blocks with the same qualified root (same previous and account)
	auto block1 = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::dev::genesis->hash (), nano::dev::genesis_key.pub, nano::Knano_ratio, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);
	auto block2 = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::dev::genesis->hash (), nano::dev::genesis_key.pub, nano::Knano_ratio * 2, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);
	auto block3 = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::dev::genesis->hash (), nano::dev::genesis_key.pub, nano::Knano_ratio * 3, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);

	nano::qualified_root root = block1->qualified_root ();
	ASSERT_EQ (root, block2->qualified_root ());
	ASSERT_EQ (root, block3->qualified_root ());

	fork_cache.put (block1);
	fork_cache.put (block2);
	fork_cache.put (block3);

	ASSERT_EQ (1, fork_cache.size ()); // Only one root in the cache
	ASSERT_TRUE (fork_cache.contains (root));

	auto blocks = fork_cache.get (root);
	ASSERT_EQ (3, blocks.size ());

	// Check if all blocks are present
	ASSERT_TRUE (std::find (blocks.begin (), blocks.end (), block1) != blocks.end ());
	ASSERT_TRUE (std::find (blocks.begin (), blocks.end (), block2) != blocks.end ());
	ASSERT_TRUE (std::find (blocks.begin (), blocks.end (), block3) != blocks.end ());
}

/*
 * Inserts multiple blocks with different roots, ensures all can be retrieved
 */
TEST (fork_cache, multiple_roots)
{
	nano::test::system system;
	nano::fork_cache_config cfg;
	nano::fork_cache fork_cache{ cfg, system.stats };

	// Create blocks with different roots
	auto block1 = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::test::random_hash (), nano::dev::genesis_key.pub, nano::Knano_ratio, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);

	nano::keypair key2;
	auto block2 = std::make_shared<nano::state_block> (key2.pub, nano::test::random_hash (), key2.pub, nano::Knano_ratio, nano::test::random_hash (), key2.prv, key2.pub, 0);

	nano::keypair key3;
	auto block3 = std::make_shared<nano::state_block> (key3.pub, nano::test::random_hash (), key3.pub, nano::Knano_ratio, nano::test::random_hash (), key3.prv, key3.pub, 0);

	nano::qualified_root root1 = block1->qualified_root ();
	nano::qualified_root root2 = block2->qualified_root ();
	nano::qualified_root root3 = block3->qualified_root ();

	// Make sure roots are different
	ASSERT_NE (root1, root2);
	ASSERT_NE (root1, root3);
	ASSERT_NE (root2, root3);

	fork_cache.put (block1);
	fork_cache.put (block2);
	fork_cache.put (block3);

	ASSERT_EQ (3, fork_cache.size ());
	ASSERT_TRUE (fork_cache.contains (root1));
	ASSERT_TRUE (fork_cache.contains (root2));
	ASSERT_TRUE (fork_cache.contains (root3));

	auto blocks1 = fork_cache.get (root1);
	ASSERT_EQ (1, blocks1.size ());
	ASSERT_EQ (block1, blocks1.front ());

	auto blocks2 = fork_cache.get (root2);
	ASSERT_EQ (1, blocks2.size ());
	ASSERT_EQ (block2, blocks2.front ());

	auto blocks3 = fork_cache.get (root3);
	ASSERT_EQ (1, blocks3.size ());
	ASSERT_EQ (block3, blocks3.front ());
}

/*
 * Tests duplicate block handling (same block twice)
 */
TEST (fork_cache, duplicate_block)
{
	nano::test::system system;
	nano::fork_cache_config cfg;
	nano::fork_cache fork_cache{ cfg, system.stats };

	auto block = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::dev::genesis->hash (), nano::dev::genesis_key.pub, nano::Knano_ratio, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);
	nano::qualified_root root = block->qualified_root ();

	// Insert the same block twice
	fork_cache.put (block);
	ASSERT_EQ (1, fork_cache.size ());
	ASSERT_EQ (1, fork_cache.get (root).size ());

	// Check the stats for insert count
	ASSERT_EQ (1, system.stats.count (nano::stat::type::fork_cache, nano::stat::detail::insert));

	fork_cache.put (block);
	ASSERT_EQ (1, fork_cache.size ());

	// Block should only be added once to the deque
	auto blocks = fork_cache.get (root);
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block, blocks.front ());
}

/*
 * Tests that when max_forks_per_root is exceeded, oldest entries are dropped
 */
TEST (fork_cache, overfill_per_root)
{
	nano::test::system system;
	nano::fork_cache_config cfg;
	cfg.max_forks_per_root = 2;
	nano::fork_cache fork_cache{ cfg, system.stats };

	// Create several blocks with the same qualified root
	auto block1 = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::dev::genesis->hash (), nano::dev::genesis_key.pub, nano::Knano_ratio, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);
	auto block2 = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::dev::genesis->hash (), nano::dev::genesis_key.pub, nano::Knano_ratio * 2, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);
	auto block3 = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::dev::genesis->hash (), nano::dev::genesis_key.pub, nano::Knano_ratio * 3, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);

	nano::qualified_root root = block1->qualified_root ();

	// Insert all three blocks
	fork_cache.put (block1);
	fork_cache.put (block2);
	fork_cache.put (block3);

	ASSERT_EQ (1, fork_cache.size ());
	auto blocks = fork_cache.get (root);
	ASSERT_EQ (2, blocks.size ()); // Only 2 blocks should be kept

	// The oldest block (block1) should have been removed
	ASSERT_FALSE (std::find (blocks.begin (), blocks.end (), block1) != blocks.end ());
	ASSERT_TRUE (std::find (blocks.begin (), blocks.end (), block2) != blocks.end ());
	ASSERT_TRUE (std::find (blocks.begin (), blocks.end (), block3) != blocks.end ());
}

/*
 * Tests that when max_size is exceeded, oldest root entries are dropped
 */
TEST (fork_cache, overfill_total)
{
	nano::test::system system;
	nano::fork_cache_config cfg;
	cfg.max_size = 2;
	nano::fork_cache fork_cache{ cfg, system.stats };

	// Create blocks with different roots
	auto block1 = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, nano::test::random_hash (), nano::dev::genesis_key.pub, nano::Knano_ratio, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);

	nano::keypair key2;
	auto block2 = std::make_shared<nano::state_block> (key2.pub, nano::test::random_hash (), key2.pub, nano::Knano_ratio, nano::test::random_hash (), key2.prv, key2.pub, 0);

	nano::keypair key3;
	auto block3 = std::make_shared<nano::state_block> (key3.pub, nano::test::random_hash (), key3.pub, nano::Knano_ratio, nano::test::random_hash (), key3.prv, key3.pub, 0);

	nano::qualified_root root1 = block1->qualified_root ();
	nano::qualified_root root2 = block2->qualified_root ();
	nano::qualified_root root3 = block3->qualified_root ();

	// Make sure roots are different
	ASSERT_NE (root1, root2);
	ASSERT_NE (root1, root3);
	ASSERT_NE (root2, root3);

	// Insert all three blocks
	fork_cache.put (block1);
	fork_cache.put (block2);
	fork_cache.put (block3);

	ASSERT_EQ (2, fork_cache.size ()); // Only 2 roots should be kept

	// The oldest root (root1) should have been removed
	ASSERT_FALSE (fork_cache.contains (root1));
	ASSERT_TRUE (fork_cache.contains (root2));
	ASSERT_TRUE (fork_cache.contains (root3));
}

/*
 * Tests a more complex scenario with multiple roots and multiple forks per root
 */
TEST (fork_cache, complex_scenario)
{
	nano::test::system system;
	nano::fork_cache_config cfg;
	cfg.max_size = 2;
	cfg.max_forks_per_root = 2;
	nano::fork_cache fork_cache{ cfg, system.stats };

	// Create multiple blocks for first root
	auto const previous1 = nano::test::random_hash ();
	auto block1a = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, previous1, nano::dev::genesis_key.pub, nano::Knano_ratio, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);
	auto block1b = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, previous1, nano::dev::genesis_key.pub, nano::Knano_ratio * 2, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);
	auto block1c = std::make_shared<nano::state_block> (nano::dev::genesis_key.pub, previous1, nano::dev::genesis_key.pub, nano::Knano_ratio * 3, nano::test::random_hash (), nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0);

	nano::qualified_root root1 = block1a->qualified_root ();

	// Create blocks for second root
	auto const previous2 = nano::test::random_hash ();
	nano::keypair key2;
	auto block2a = std::make_shared<nano::state_block> (key2.pub, previous2, key2.pub, nano::Knano_ratio, nano::test::random_hash (), key2.prv, key2.pub, 0);
	auto block2b = std::make_shared<nano::state_block> (key2.pub, previous2, key2.pub, nano::Knano_ratio * 2, nano::test::random_hash (), key2.prv, key2.pub, 0);

	nano::qualified_root root2 = block2a->qualified_root ();

	// Create block for third root
	nano::keypair key3;
	auto const previous3 = nano::test::random_hash ();
	auto block3 = std::make_shared<nano::state_block> (key3.pub, previous3, key3.pub, nano::Knano_ratio, nano::test::random_hash (), key3.prv, key3.pub, 0);

	nano::qualified_root root3 = block3->qualified_root ();

	// Make sure roots are different
	ASSERT_NE (root1, root2);
	ASSERT_NE (root1, root3);
	ASSERT_NE (root2, root3);

	// Insert blocks for first root
	fork_cache.put (block1a);
	fork_cache.put (block1b);
	fork_cache.put (block1c);

	// First root should have max_forks_per_root=2 blocks, with the oldest dropped
	ASSERT_EQ (1, fork_cache.size ());
	auto blocks1 = fork_cache.get (root1);
	ASSERT_EQ (2, blocks1.size ());
	ASSERT_FALSE (std::find (blocks1.begin (), blocks1.end (), block1a) != blocks1.end ()); // Oldest should be dropped
	ASSERT_TRUE (std::find (blocks1.begin (), blocks1.end (), block1b) != blocks1.end ());
	ASSERT_TRUE (std::find (blocks1.begin (), blocks1.end (), block1c) != blocks1.end ());

	// Insert blocks for second root
	fork_cache.put (block2a);
	fork_cache.put (block2b);

	// Still within max_size=2, so both roots should be present
	ASSERT_EQ (2, fork_cache.size ());
	ASSERT_TRUE (fork_cache.contains (root1));
	ASSERT_TRUE (fork_cache.contains (root2));

	auto blocks2 = fork_cache.get (root2);
	ASSERT_EQ (2, blocks2.size ());

	// Insert block for third root
	fork_cache.put (block3);

	// Should exceed max_size, oldest root (root1) should be dropped
	ASSERT_EQ (2, fork_cache.size ());
	ASSERT_FALSE (fork_cache.contains (root1)); // Oldest root dropped
	ASSERT_TRUE (fork_cache.contains (root2));
	ASSERT_TRUE (fork_cache.contains (root3));
}

/*
 * Tests getting a non-existent root
 */
TEST (fork_cache, nonexistent)
{
	nano::test::system system;
	nano::fork_cache_config cfg;
	nano::fork_cache fork_cache{ cfg, system.stats };

	nano::qualified_root nonexistent_root;
	auto blocks = fork_cache.get (nonexistent_root);
	ASSERT_TRUE (blocks.empty ());
}