#include <nano/node/prioritization.hpp>
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
						 .build_shared ();
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
						 .build_shared ();
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
						 .build_shared ();
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
						 .build_shared ();
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
						 .build_shared ();
	return result;
}

TEST (prioritization, construction)
{
	nano::prioritization prioritization;
	ASSERT_EQ (0, prioritization.size ());
	ASSERT_TRUE (prioritization.empty ());
	ASSERT_EQ (62, prioritization.bucket_count ());
}

TEST (prioritization, index_min)
{
	nano::prioritization prioritization;
	ASSERT_EQ (0, prioritization.index (std::numeric_limits<nano::uint128_t>::min ()));
}

TEST (prioritization, index_max)
{
	nano::prioritization prioritization;
	ASSERT_EQ (prioritization.bucket_count () - 1, prioritization.index (std::numeric_limits<nano::uint128_t>::max ()));
}

TEST (prioritization, insert_Gxrb)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0 ());
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (48));
}

TEST (prioritization, insert_Mxrb)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block1 ());
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (13));
}

// Test two blocks with the same priority
TEST (prioritization, insert_same_priority)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0 ());
	prioritization.push (1000, block2 ());
	ASSERT_EQ (2, prioritization.size ());
	ASSERT_EQ (2, prioritization.bucket_size (48));
}

// Test the same block inserted multiple times
TEST (prioritization, insert_duplicate)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0 ());
	prioritization.push (1000, block0 ());
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (48));
}

TEST (prioritization, insert_older)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0 ());
	prioritization.push (1100, block2 ());
	ASSERT_EQ (block0 (), prioritization.top ());
	prioritization.pop ();
	ASSERT_EQ (block2 (), prioritization.top ());
	prioritization.pop ();
}

TEST (prioritization, pop)
{
	nano::prioritization prioritization;
	ASSERT_TRUE (prioritization.empty ());
	prioritization.push (1000, block0 ());
	ASSERT_FALSE (prioritization.empty ());
	prioritization.pop ();
	ASSERT_TRUE (prioritization.empty ());
}

TEST (prioritization, top_one)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0 ());
	ASSERT_EQ (block0 (), prioritization.top ());
}

TEST (prioritization, top_two)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0 ());
	prioritization.push (1, block1 ());
	ASSERT_EQ (block0 (), prioritization.top ());
	prioritization.pop ();
	ASSERT_EQ (block1 (), prioritization.top ());
	prioritization.pop ();
	ASSERT_TRUE (prioritization.empty ());
}

TEST (prioritization, top_round_robin)
{
	nano::prioritization prioritization;
	prioritization.push (1000, blockzero ());
	ASSERT_EQ (blockzero (), prioritization.top ());
	prioritization.push (1000, block0 ());
	prioritization.push (1000, block1 ());
	prioritization.push (1100, block3 ());
	prioritization.pop (); // blockzero
	EXPECT_EQ (block1 (), prioritization.top ());
	prioritization.pop ();
	EXPECT_EQ (block0 (), prioritization.top ());
	prioritization.pop ();
	EXPECT_EQ (block3 (), prioritization.top ());
	prioritization.pop ();
	EXPECT_TRUE (prioritization.empty ());
}

TEST (prioritization, trim_normal)
{
	nano::prioritization prioritization{ 1 };
	prioritization.push (1000, block0 ());
	prioritization.push (1100, block2 ());
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (block0 (), prioritization.top ());
}

TEST (prioritization, trim_reverse)
{
	nano::prioritization prioritization{ 1 };
	prioritization.push (1100, block2 ());
	prioritization.push (1000, block0 ());
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (block0 (), prioritization.top ());
}

TEST (prioritization, trim_even)
{
	nano::prioritization prioritization{ 2 };
	prioritization.push (1000, block0 ());
	prioritization.push (1100, block2 ());
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (block0 (), prioritization.top ());
	prioritization.push (1000, block1 ());
	ASSERT_EQ (2, prioritization.size ());
	ASSERT_EQ (block0 (), prioritization.top ());
	prioritization.pop ();
	ASSERT_EQ (block1 (), prioritization.top ());
}
