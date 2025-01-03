#include <nano/node/bucketing.hpp>

#include <gtest/gtest.h>

#include <algorithm>

TEST (bucketing, construction)
{
	nano::bucketing bucketing;
	ASSERT_EQ (63, bucketing.size ());
}

TEST (bucketing, zero_index)
{
	nano::bucketing bucketing;
	ASSERT_EQ (0, bucketing.bucket_index (0));
}

TEST (bucketing, raw_index)
{
	nano::bucketing bucketing;
	ASSERT_EQ (0, bucketing.bucket_index (nano::raw_ratio));
}

TEST (bucketing, nano_index)
{
	nano::bucketing bucketing;
	ASSERT_EQ (14, bucketing.bucket_index (nano::nano_ratio));
}

TEST (bucketing, Knano_index)
{
	nano::bucketing bucketing;
	ASSERT_EQ (49, bucketing.bucket_index (nano::Knano_ratio));
}

TEST (bucketing, max_index)
{
	nano::bucketing bucketing;
	ASSERT_EQ (62, bucketing.bucket_index (std::numeric_limits<nano::amount::underlying_type>::max ()));
}

TEST (bucketing, indices)
{
	nano::bucketing bucketing;
	auto indices = bucketing.bucket_indices ();
	ASSERT_EQ (63, indices.size ());
	ASSERT_EQ (indices.size (), bucketing.size ());

	// Check that the indices are in ascending order
	ASSERT_TRUE (std::adjacent_find (indices.begin (), indices.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs >= rhs;
	})
	== indices.end ());
}