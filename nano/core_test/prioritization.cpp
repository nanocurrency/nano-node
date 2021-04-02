#include <nano/node/prioritization.hpp>

#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

#include <unordered_set>

static nano::keypair key0;
static nano::keypair key1;
static nano::keypair key2;
static auto block0 = std::make_shared<nano::state_block> (key0.pub, 0, key0.pub, nano::Gxrb_ratio, 0, key0.prv, key0.pub, 0);
static auto block1 = std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, nano::Mxrb_ratio, 0, key1.prv, key1.pub, 0);
static auto block2 = std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, nano::Gxrb_ratio, 0, key2.prv, key2.pub, 0);

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
	prioritization.push (1000, block0);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (110));
}

TEST (prioritization, insert_one)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block1);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (100));
}

TEST (prioritization, insert_same_priority)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0);
	prioritization.push (1000, block2);
	ASSERT_EQ (2, prioritization.size ());
	ASSERT_EQ (2, prioritization.bucket_size (110));
}

TEST (prioritization, insert_duplicate)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0);
	prioritization.push (1000, block0);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (110));
}

TEST (prioritization, pop)
{
	nano::prioritization prioritization;
	ASSERT_TRUE (prioritization.empty ());
	prioritization.push (1000, block0);
	ASSERT_FALSE (prioritization.empty ());
	prioritization.pop ();
	ASSERT_TRUE (prioritization.empty ());
}

TEST (prioritization, top_one)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0);
	ASSERT_EQ (block0, prioritization.top ());
}

TEST (prioritization, top_two)
{
	nano::prioritization prioritization;
	prioritization.push (1000, block0);
	prioritization.push (1, block1);
	ASSERT_EQ (block0, prioritization.top ());
	prioritization.pop ();
	ASSERT_EQ (block1, prioritization.top ());
	prioritization.pop ();
	ASSERT_TRUE (prioritization.empty ());
}

