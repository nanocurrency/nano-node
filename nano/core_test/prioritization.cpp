#include <nano/node/prioritization.hpp>

#include <gtest/gtest.h>

TEST (prioritization, construction)
{
	nano::prioritization prioritization;
	ASSERT_EQ (0, prioritization.size ());
	ASSERT_EQ (128, prioritization.bucket_count ());
}

TEST (prioritization, insert_one)
{
	nano::prioritization prioritization;
	prioritization.insert (1000, 1, 0);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (0));
}

TEST (prioritization, insert_same_priority)
{
	nano::prioritization prioritization;
	prioritization.insert (1000, 1, 0);
	prioritization.insert (1000, 1, 1);
	ASSERT_EQ (2, prioritization.size ());
	ASSERT_EQ (2, prioritization.bucket_size (0));
}

TEST (prioritization, insert_duplicate)
{
	nano::prioritization prioritization;
	prioritization.insert (1000, 1, 0);
	prioritization.insert (1000, 1, 0);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (0));
}

TEST (prioritization, insert_max)
{
	nano::prioritization prioritization;
	prioritization.insert (1000, std::numeric_limits<nano::uint128_t>::max (), 0);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (127));
}
