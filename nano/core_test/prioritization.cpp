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
	static std::shared_ptr<nano::state_block> result = std::make_shared<nano::state_block> (keyzero ().pub, 0, keyzero ().pub, 0, 0, keyzero ().prv, keyzero ().pub, 0);
	return result;
}
std::shared_ptr<nano::state_block> & block0 ()
{
	static std::shared_ptr<nano::state_block> result = std::make_shared<nano::state_block> (key0 ().pub, 0, key0 ().pub, nano::MBAN_ratio, 0, key0 ().prv, key0 ().pub, 0);
	return result;
}
std::shared_ptr<nano::state_block> & block1 ()
{
	static std::shared_ptr<nano::state_block> result = std::make_shared<nano::state_block> (key1 ().pub, 0, key1 ().pub, nano::BAN_ratio, 0, key1 ().prv, key1 ().pub, 0);
	return result;
}
std::shared_ptr<nano::state_block> & block2 ()
{
	static std::shared_ptr<nano::state_block> result = std::make_shared<nano::state_block> (key2 ().pub, 0, key2 ().pub, nano::MBAN_ratio, 0, key2 ().prv, key2 ().pub, 0);
	return result;
}
std::shared_ptr<nano::state_block> & block3 ()
{
	static std::shared_ptr<nano::state_block> result = std::make_shared<nano::state_block> (key3 ().pub, 0, key3 ().pub, nano::BAN_ratio, 0, key3 ().prv, key3 ().pub, 0);
	return result;
}

TEST (prioritization, construction)
{
	nano::prioritization prioritization;
	ASSERT_EQ (0, prioritization.size ());
	ASSERT_TRUE (prioritization.empty ());
	ASSERT_EQ (129, prioritization.bucket_count ());
}

TEST (prioritization, insert_zero)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0 ());
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (110));
}

TEST (prioritization, insert_one)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block1 ());
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (100));
}

TEST (prioritization, insert_same_priority)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0 ());
	prioritization.push (1000, block2 ());
	ASSERT_EQ (2, prioritization.size ());
	ASSERT_EQ (2, prioritization.bucket_size (110));
}

TEST (prioritization, insert_duplicate)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0 ());
	prioritization.push (1000, block0 ());
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (110));
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
