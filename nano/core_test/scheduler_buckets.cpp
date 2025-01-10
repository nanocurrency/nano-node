#include <celerix/lib/blocks.hpp>
#include <celerix/secure/common.hpp>

#include <gtest/gtest.h>

#include <unordered_set>

celerix::keypair & keyzero ()
{
	static celerix::keypair result;
	return result;
}
celerix::keypair & key0 ()
{
	static celerix::keypair result;
	return result;
}
celerix::keypair & key1 ()
{
	static celerix::keypair result;
	return result;
}
celerix::keypair & key2 ()
{
	static celerix::keypair result;
	return result;
}
celerix::keypair & key3 ()
{
	static celerix::keypair result;
	return result;
}
std::shared_ptr<celerix::state_block> & blockzero ()
{
	celerix::block_builder builder;
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
std::shared_ptr<celerix::state_block> & block0 ()
{
	celerix::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key0 ().pub)
						 .previous (0)
						 .representative (key0 ().pub)
						 .balance (celerix::Kcelerix_ratio)
						 .link (0)
						 .sign (key0 ().prv, key0 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<celerix::state_block> & block1 ()
{
	celerix::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key1 ().pub)
						 .previous (0)
						 .representative (key1 ().pub)
						 .balance (celerix::celerix_ratio)
						 .link (0)
						 .sign (key1 ().prv, key1 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<celerix::state_block> & block2 ()
{
	celerix::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key2 ().pub)
						 .previous (0)
						 .representative (key2 ().pub)
						 .balance (celerix::Kcelerix_ratio)
						 .link (0)
						 .sign (key2 ().prv, key2 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<celerix::state_block> & block3 ()
{
	celerix::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key3 ().pub)
						 .previous (0)
						 .representative (key3 ().pub)
						 .balance (celerix::celerix_ratio)
						 .link (0)
						 .sign (key3 ().prv, key3 ().pub)
						 .work (0)
						 .build ();
	return result;
}

/*
TEST (buckets, construction)
{
	celerix::scheduler::buckets buckets;
	ASSERT_EQ (0, buckets.size ());
	ASSERT_TRUE (buckets.empty ());
	ASSERT_EQ (63, buckets.bucket_count ());
}

TEST (buckets, insert_Kcelerix)
{
	celerix::scheduler::buckets buckets;
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (1, buckets.bucket_size (49));
}

TEST (buckets, insert_Mxrb)
{
	celerix::scheduler::buckets buckets;
	buckets.push (1000, block1 (), celerix::celerix_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (1, buckets.bucket_size (14));
}

// Test two blocks with the same priority
TEST (buckets, insert_same_priority)
{
	celerix::scheduler::buckets buckets;
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	buckets.push (1000, block2 (), celerix::Kcelerix_ratio);
	ASSERT_EQ (2, buckets.size ());
	ASSERT_EQ (2, buckets.bucket_size (49));
}

// Test the same block inserted multiple times
TEST (buckets, insert_duplicate)
{
	celerix::scheduler::buckets buckets;
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (1, buckets.bucket_size (49));
}

TEST (buckets, insert_older)
{
	celerix::scheduler::buckets buckets;
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	buckets.push (1100, block2 (), celerix::Kcelerix_ratio);
	ASSERT_EQ (block0 (), buckets.top ());
	buckets.pop ();
	ASSERT_EQ (block2 (), buckets.top ());
	buckets.pop ();
}

TEST (buckets, pop)
{
	celerix::scheduler::buckets buckets;
	ASSERT_TRUE (buckets.empty ());
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	ASSERT_FALSE (buckets.empty ());
	buckets.pop ();
	ASSERT_TRUE (buckets.empty ());
}

TEST (buckets, top_one)
{
	celerix::scheduler::buckets buckets;
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	ASSERT_EQ (block0 (), buckets.top ());
}

TEST (buckets, top_two)
{
	celerix::scheduler::buckets buckets;
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	buckets.push (1, block1 (), celerix::celerix_ratio);
	ASSERT_EQ (block0 (), buckets.top ());
	buckets.pop ();
	ASSERT_EQ (block1 (), buckets.top ());
	buckets.pop ();
	ASSERT_TRUE (buckets.empty ());
}

TEST (buckets, top_round_robin)
{
	celerix::scheduler::buckets buckets;
	buckets.push (1000, blockzero (), 0);
	ASSERT_EQ (blockzero (), buckets.top ());
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	buckets.push (1000, block1 (), celerix::celerix_ratio);
	buckets.push (1100, block3 (), celerix::celerix_ratio);
	buckets.pop (); // blockzero
	EXPECT_EQ (block1 (), buckets.top ());
	buckets.pop ();
	EXPECT_EQ (block0 (), buckets.top ());
	buckets.pop ();
	EXPECT_EQ (block3 (), buckets.top ());
	buckets.pop ();
	EXPECT_TRUE (buckets.empty ());
}

TEST (buckets, trim_normal)
{
	celerix::scheduler::buckets buckets{ 1 };
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	buckets.push (1100, block2 (), celerix::Kcelerix_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (block0 (), buckets.top ());
}

TEST (buckets, trim_reverse)
{
	celerix::scheduler::buckets buckets{ 1 };
	buckets.push (1100, block2 (), celerix::Kcelerix_ratio);
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (block0 (), buckets.top ());
}

TEST (buckets, trim_even)
{
	celerix::scheduler::buckets buckets{ 2 };
	buckets.push (1000, block0 (), celerix::Kcelerix_ratio);
	buckets.push (1100, block2 (), celerix::Kcelerix_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (block0 (), buckets.top ());
	buckets.push (1000, block1 (), celerix::celerix_ratio);
	ASSERT_EQ (2, buckets.size ());
	ASSERT_EQ (block0 (), buckets.top ());
	buckets.pop ();
	ASSERT_EQ (block1 (), buckets.top ());
}
*/