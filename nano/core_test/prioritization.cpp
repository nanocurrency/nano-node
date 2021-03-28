#include <nano/node/prioritization.hpp>

#include <gtest/gtest.h>

#include <unordered_set>

TEST (prioritization, construction)
{
	nano::prioritization prioritization;
	ASSERT_EQ (0, prioritization.size ());
	ASSERT_EQ (129, prioritization.bucket_count ());
}

TEST (prioritization, insert_zero)
{
	nano::prioritization prioritization;
	prioritization.insert (1000, 0, 0);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (0));
}

TEST (prioritization, insert_one)
{
	nano::prioritization prioritization;
	prioritization.insert (1000, 1, 0);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (1));
}

TEST (prioritization, insert_same_priority)
{
	nano::prioritization prioritization;
	prioritization.insert (1000, 1, 0);
	prioritization.insert (1000, 1, 1);
	ASSERT_EQ (2, prioritization.size ());
	ASSERT_EQ (2, prioritization.bucket_size (1));
}

TEST (prioritization, insert_duplicate)
{
	nano::prioritization prioritization;
	prioritization.insert (1000, 1, 0);
	prioritization.insert (1000, 1, 0);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (1));
}

TEST (prioritization, insert_max)
{
	nano::prioritization prioritization;
	prioritization.insert (1000, std::numeric_limits<nano::uint128_t>::max (), 0);
	ASSERT_EQ (1, prioritization.size ());
	ASSERT_EQ (1, prioritization.bucket_size (128));
}

TEST (prioritization, fetch_empty)
{
	std::unordered_set<nano::account> filter;
	nano::prioritization prioritization;
	ASSERT_TRUE (prioritization.fetch (filter).is_zero ());
}

TEST (prioritization, fetch_one)
{
	std::unordered_set<nano::account> filter;
	nano::prioritization prioritization;
	prioritization.insert (1000, 256, 42);
	ASSERT_EQ (nano::account{ 42 }, prioritization.fetch (filter));
}

TEST (prioritization, fetch_filter_priority)
{
	std::unordered_set<nano::account> filter;
	filter.emplace (42);
	nano::prioritization prioritization;
	prioritization.insert (1000, 256, 42);
	prioritization.insert (1001, 512, 43);
	ASSERT_EQ (nano::account{ 43 }, prioritization.fetch (filter));
}

TEST (prioritization, fetch_filter_bucket)
{
	std::unordered_set<nano::account> filter;
	filter.emplace (42);
	nano::prioritization prioritization;
	prioritization.insert (1000, 256, 42);
	prioritization.insert (999, 256, 43);
	ASSERT_EQ (nano::account{ 43 }, prioritization.fetch (filter));
}
