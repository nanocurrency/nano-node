#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/utility.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>

using namespace std::chrono_literals;

namespace
{
unsigned max_unchecked_blocks = 65536;

class context
{
public:
	context () :
		unchecked{ max_unchecked_blocks, stats, false }
	{
	}
	nano::stats stats;
	nano::unchecked_map unchecked;
};
std::shared_ptr<nano::block> block ()
{
	nano::block_builder builder;
	return builder.state ()
	.account (nano::dev::genesis_key.pub)
	.previous (nano::dev::genesis->hash ())
	.representative (nano::dev::genesis_key.pub)
	.balance (nano::dev::constants.genesis_amount - 1)
	.link (nano::dev::genesis_key.pub)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (0)
	.build ();
}
}

TEST (unchecked_map, construction)
{
	context context;
}

TEST (unchecked_map, put_one)
{
	context context;
	nano::unchecked_info info{ block () };
	context.unchecked.put (info.block->previous (), info);
}

TEST (block_store, one_bootstrap)
{
	nano::test::system system{};
	nano::unchecked_map unchecked{ max_unchecked_blocks, system.stats, false };
	nano::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (0)
				  .destination (1)
				  .balance (2)
				  .sign (nano::keypair ().prv, 4)
				  .work (5)
				  .build ();
	unchecked.put (block1->hash (), nano::unchecked_info{ block1 });
	auto check_block_is_listed = [&] (nano::block_hash const & block_hash_a) {
		return unchecked.get (block_hash_a).size () > 0;
	};
	// Waits for the block1 to get saved in the database
	ASSERT_TIMELY (10s, check_block_is_listed (block1->hash ()));
	std::vector<nano::block_hash> dependencies;
	unchecked.for_each ([&dependencies] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
		dependencies.push_back (key.key ());
	});
	auto hash1 = dependencies[0];
	ASSERT_EQ (block1->hash (), hash1);
	auto blocks = unchecked.get (hash1);
	ASSERT_EQ (1, blocks.size ());
	auto block2 = blocks[0].block;
	ASSERT_EQ (*block1, *block2);
}

// This test checks for basic operations in the unchecked table such as putting a new block, retrieving it, and
// deleting it from the database
TEST (unchecked, simple)
{
	nano::test::system system{};
	nano::unchecked_map unchecked{ max_unchecked_blocks, system.stats, false };
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (0)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (5)
				 .build ();
	// Asserts the block wasn't added yet to the unchecked table
	auto block_listing1 = unchecked.get (block->previous ());
	ASSERT_TRUE (block_listing1.empty ());
	// Enqueues a block to be saved on the unchecked table
	unchecked.put (block->previous (), nano::unchecked_info (block));
	// Waits for the block to get written in the database
	auto check_block_is_listed = [&] (nano::block_hash const & block_hash_a) {
		return unchecked.get (block_hash_a).size () > 0;
	};
	ASSERT_TIMELY (5s, check_block_is_listed (block->previous ()));
	// Retrieves the block from the database
	auto block_listing2 = unchecked.get (block->previous ());
	ASSERT_FALSE (block_listing2.empty ());
	// Asserts the added block is equal to the retrieved one
	ASSERT_EQ (*block, *(block_listing2[0].block));
	// Deletes the block from the database
	unchecked.del (nano::unchecked_key (block->previous (), block->hash ()));
	// Asserts the block is deleted
	auto block_listing3 = unchecked.get (block->previous ());
	ASSERT_TRUE (block_listing3.empty ());
}

// This test ensures the unchecked table is able to receive more than one block
TEST (unchecked, multiple)
{
	nano::test::system system{};
	if (nano::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	nano::unchecked_map unchecked{ max_unchecked_blocks, system.stats, false };
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (4)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (5)
				 .build ();
	// Asserts the block wasn't added yet to the unchecked table
	auto block_listing1 = unchecked.get (block->previous ());
	ASSERT_TRUE (block_listing1.empty ());
	// Enqueues the first block
	unchecked.put (block->previous (), nano::unchecked_info (block));
	// Enqueues a second block
	unchecked.put (block->source (), nano::unchecked_info (block));
	auto check_block_is_listed = [&] (nano::block_hash const & block_hash_a) {
		return unchecked.get (block_hash_a).size () > 0;
	};
	// Waits for and asserts the first block gets saved in the database
	ASSERT_TIMELY (5s, check_block_is_listed (block->previous ()));
	// Waits for and asserts the second block gets saved in the database
	ASSERT_TIMELY (5s, check_block_is_listed (block->source ()));
}

// This test ensures that a block can't occur twice in the unchecked table.
TEST (unchecked, double_put)
{
	nano::test::system system{};
	nano::unchecked_map unchecked{ max_unchecked_blocks, system.stats, false };
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (4)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (5)
				 .build ();
	// Asserts the block wasn't added yet to the unchecked table
	auto block_listing1 = unchecked.get (block->previous ());
	ASSERT_TRUE (block_listing1.empty ());
	// Enqueues the block to be saved in the unchecked table
	unchecked.put (block->previous (), nano::unchecked_info (block));
	// Enqueues the block again in an attempt to have it there twice
	unchecked.put (block->previous (), nano::unchecked_info (block));
	auto check_block_is_listed = [&] (nano::block_hash const & block_hash_a) {
		return unchecked.get (block_hash_a).size () > 0;
	};
	// Waits for and asserts the block was added at least once
	ASSERT_TIMELY (5s, check_block_is_listed (block->previous ()));
	// Asserts the block was added at most once -- this is objective of this test.
	auto block_listing2 = unchecked.get (block->previous ());
	ASSERT_EQ (block_listing2.size (), 1);
}

// Tests that recurrent get calls return the correct values
TEST (unchecked, multiple_get)
{
	nano::test::system system{};
	nano::unchecked_map unchecked{ max_unchecked_blocks, system.stats, false };
	// Instantiates three blocks
	nano::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (4)
				  .destination (1)
				  .balance (2)
				  .sign (nano::keypair ().prv, 4)
				  .work (5)
				  .build ();
	auto block2 = builder
				  .send ()
				  .previous (3)
				  .destination (1)
				  .balance (2)
				  .sign (nano::keypair ().prv, 4)
				  .work (5)
				  .build ();
	auto block3 = builder
				  .send ()
				  .previous (5)
				  .destination (1)
				  .balance (2)
				  .sign (nano::keypair ().prv, 4)
				  .work (5)
				  .build ();
	// Add the blocks' info to the unchecked table
	unchecked.put (block1->previous (), nano::unchecked_info (block1)); // unchecked1
	unchecked.put (block1->hash (), nano::unchecked_info (block1)); // unchecked2
	unchecked.put (block2->previous (), nano::unchecked_info (block2)); // unchecked3
	unchecked.put (block1->previous (), nano::unchecked_info (block2)); // unchecked1
	unchecked.put (block1->hash (), nano::unchecked_info (block2)); // unchecked2
	unchecked.put (block3->previous (), nano::unchecked_info (block3));
	unchecked.put (block3->hash (), nano::unchecked_info (block3)); // unchecked4
	unchecked.put (block1->previous (), nano::unchecked_info (block3)); // unchecked1

	// count the number of blocks in the unchecked table by counting them one by one
	// we cannot trust the count() method if the backend is rocksdb
	auto count_unchecked_blocks_one_by_one = [&unchecked] () {
		size_t count = 0;
		unchecked.for_each ([&count] (nano::unchecked_key const & key, nano::unchecked_info const & info) {
			++count;
		});
		return count;
	};

	// Waits for the blocks to get saved in the database
	ASSERT_TIMELY_EQ (5s, 8, count_unchecked_blocks_one_by_one ());

	std::vector<nano::block_hash> unchecked1;
	// Asserts the entries will be found for the provided key
	auto unchecked1_blocks = unchecked.get (block1->previous ());
	ASSERT_EQ (unchecked1_blocks.size (), 3);
	for (auto & i : unchecked1_blocks)
	{
		unchecked1.push_back (i.block->hash ());
	}
	// Asserts the payloads where correclty saved
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block1->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block2->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block3->hash ()) != unchecked1.end ());
	std::vector<nano::block_hash> unchecked2;
	// Asserts the entries will be found for the provided key
	auto unchecked2_blocks = unchecked.get (block1->hash ());
	ASSERT_EQ (unchecked2_blocks.size (), 2);
	for (auto & i : unchecked2_blocks)
	{
		unchecked2.push_back (i.block->hash ());
	}
	// Asserts the payloads where correctly saved
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block1->hash ()) != unchecked2.end ());
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block2->hash ()) != unchecked2.end ());
	// Asserts the entry is found by the key and the payload is saved
	auto unchecked3 = unchecked.get (block2->previous ());
	ASSERT_EQ (unchecked3.size (), 1);
	ASSERT_EQ (unchecked3[0].block->hash (), block2->hash ());
	// Asserts the entry is found by the key and the payload is saved
	auto unchecked4 = unchecked.get (block3->hash ());
	ASSERT_EQ (unchecked4.size (), 1);
	ASSERT_EQ (unchecked4[0].block->hash (), block3->hash ());
	// Asserts no entry is found for a block that wasn't added
	auto unchecked5 = unchecked.get (block2->hash ());
	ASSERT_EQ (unchecked5.size (), 0);
}
