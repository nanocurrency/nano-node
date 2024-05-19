#include <nano/lib/blocks.hpp>
#include <nano/node/scheduler/bucket.hpp>
#include <nano/node/scheduler/buckets.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

#include <unordered_set>

nano::keypair & keyzero ()
{
	static nano::keypair result;
	return result;
}
nano::keypair & key0 ()
{
	static nano::keypair result;
	return result;
}
nano::keypair & key1 ()
{
	static nano::keypair result;
	return result;
}
nano::keypair & key2 ()
{
	static nano::keypair result;
	return result;
}
nano::keypair & key3 ()
{
	static nano::keypair result;
	return result;
}
std::shared_ptr<nano::state_block> & blockzero ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (keyzero ().pub)
						 .previous (0)
						 .representative (keyzero ().pub)
						 .balance (0)
						 .link (0)
						 .sign (keyzero ().prv, keyzero ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<nano::state_block> & block0 ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key0 ().pub)
						 .previous (0)
						 .representative (key0 ().pub)
						 .balance (nano::Gxrb_ratio)
						 .link (0)
						 .sign (key0 ().prv, key0 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<nano::state_block> & block1 ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key1 ().pub)
						 .previous (0)
						 .representative (key1 ().pub)
						 .balance (nano::Mxrb_ratio)
						 .link (0)
						 .sign (key1 ().prv, key1 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<nano::state_block> & block2 ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key2 ().pub)
						 .previous (0)
						 .representative (key2 ().pub)
						 .balance (nano::Gxrb_ratio)
						 .link (0)
						 .sign (key2 ().prv, key2 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<nano::state_block> & block3 ()
{
	nano::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key3 ().pub)
						 .previous (0)
						 .representative (key3 ().pub)
						 .balance (nano::Mxrb_ratio)
						 .link (0)
						 .sign (key3 ().prv, key3 ().pub)
						 .work (0)
						 .build ();
	return result;
}

TEST (buckets, construction)
{
	nano::scheduler::buckets buckets;
	ASSERT_EQ (0, buckets.size ());
	ASSERT_TRUE (buckets.empty ());
	ASSERT_EQ (62, buckets.bucket_count ());
}

TEST (buckets, insert_Gxrb)
{
	nano::scheduler::buckets buckets;
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (1, buckets.bucket_size (nano::Gxrb_ratio));
}

TEST (buckets, insert_Mxrb)
{
	nano::scheduler::buckets buckets;
	buckets.bucket (nano::Mxrb_ratio).push (1000, block1 ());
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (1, buckets.bucket_size (nano::Mxrb_ratio));
}

// Test two blocks with the same priority
TEST (buckets, insert_same_priority)
{
	nano::scheduler::buckets buckets;
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	buckets.bucket (nano::Gxrb_ratio).push (1000, block2 ());
	ASSERT_EQ (2, buckets.size ());
	ASSERT_EQ (2, buckets.bucket_size (nano::Gxrb_ratio));
}

// Test the same block inserted multiple times
TEST (buckets, insert_duplicate)
{
	nano::scheduler::buckets buckets;
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (1, buckets.bucket_size (nano::Gxrb_ratio));
}

TEST (buckets, insert_older)
{
	nano::scheduler::buckets buckets;
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	buckets.bucket (nano::Gxrb_ratio).push (1100, block2 ());
	ASSERT_EQ (block0 (), buckets.bucket (nano::Gxrb_ratio).top ());
	buckets.bucket (nano::Gxrb_ratio).pop ();
	ASSERT_EQ (block2 (), buckets.bucket (nano::Gxrb_ratio).top ());
	buckets.bucket (nano::Gxrb_ratio).pop ();
}

TEST (buckets, pop)
{
	nano::scheduler::buckets buckets;
	ASSERT_TRUE (buckets.empty ());
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	ASSERT_FALSE (buckets.empty ());
	buckets.bucket (nano::Gxrb_ratio).pop ();
	ASSERT_TRUE (buckets.empty ());
}

TEST (buckets, top_one)
{
	nano::scheduler::buckets buckets;
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	ASSERT_EQ (block0 (), buckets.bucket (nano::Gxrb_ratio).top ());
}

TEST (buckets, top_two)
{
	nano::scheduler::buckets buckets;
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	buckets.bucket (nano::Mxrb_ratio).push (1, block1 ());
	ASSERT_EQ (block0 (), buckets.bucket (nano::Gxrb_ratio).top ());
	buckets.bucket (nano::Gxrb_ratio).pop ();
	ASSERT_EQ (block1 (), buckets.bucket (nano::Mxrb_ratio).top ());
	buckets.bucket (nano::Mxrb_ratio).pop ();
	ASSERT_TRUE (buckets.empty ());
}

TEST (buckets, top_round_robin)
{
	nano::scheduler::buckets buckets;
	buckets.bucket (0).push (1000, blockzero ());
	ASSERT_EQ (blockzero (), buckets.bucket (0).top ());
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	buckets.bucket (nano::Mxrb_ratio).push (1000, block1 ());
	buckets.bucket (nano::Mxrb_ratio).push (1100, block3 ());
	buckets.bucket (0).pop (); // blockzero
	EXPECT_EQ (block1 (), buckets.bucket (nano::Mxrb_ratio).top ());
	buckets.bucket (nano::Mxrb_ratio).pop ();
	EXPECT_EQ (block0 (), buckets.bucket (nano::Gxrb_ratio).top ());
	buckets.bucket (nano::Gxrb_ratio).pop ();
	EXPECT_EQ (block3 (), buckets.bucket (nano::Mxrb_ratio).top ());
	buckets.bucket (nano::Mxrb_ratio).pop ();
	EXPECT_TRUE (buckets.empty ());
}

TEST (buckets, trim_normal)
{
	nano::scheduler::buckets buckets{ 1 };
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	buckets.bucket (nano::Gxrb_ratio).push (1100, block2 ());
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (block0 (), buckets.bucket (nano::Gxrb_ratio).top ());
}

TEST (buckets, trim_reverse)
{
	nano::scheduler::buckets buckets{ 1 };
	buckets.bucket (nano::Gxrb_ratio).push (1100, block2 ());
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (block0 (), buckets.bucket (nano::Gxrb_ratio).top ());
}

TEST (buckets, trim_even)
{
	nano::scheduler::buckets buckets{ 1 };
	buckets.bucket (nano::Gxrb_ratio).push (1000, block0 ());
	buckets.bucket (nano::Gxrb_ratio).push (1100, block2 ());
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (block0 (), buckets.bucket (nano::Gxrb_ratio).top ());
	buckets.bucket (nano::Mxrb_ratio).push (1000, block1 ());
	ASSERT_EQ (2, buckets.size ());
	ASSERT_EQ (block0 (), buckets.bucket (nano::Gxrb_ratio).top ());
	buckets.bucket (nano::Gxrb_ratio).pop ();
	ASSERT_EQ (block1 (), buckets.bucket (nano::Mxrb_ratio).top ());
}
