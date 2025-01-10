#include <celerix/node/bucketing.hpp>

#include <gtest/gtest.h>

#include <algorithm>

TEST (bucketing, construction)
{
	celerix::bucketing bucketing;
	ASSERT_EQ (63, bucketing.size ());
}

TEST (bucketing, zero_index)
{
	celerix::bucketing bucketing;
	ASSERT_EQ (0, bucketing.bucket_index (0));
}

TEST (bucketing, raw_index)
{
	celerix::bucketing bucketing;
	ASSERT_EQ (0, bucketing.bucket_index (celerix::raw_ratio));
}

TEST (bucketing, celerix_index)
{
	celerix::bucketing bucketing;
	ASSERT_EQ (14, bucketing.bucket_index (celerix::celerix_ratio));
}

TEST (bucketing, Kcelerix_index)
{
	celerix::bucketing bucketing;
	ASSERT_EQ (49, bucketing.bucket_index (celerix::Kcelerix_ratio));
}

TEST (bucketing, max_index)
{
	celerix::bucketing bucketing;
	ASSERT_EQ (62, bucketing.bucket_index (std::numeric_limits<celerix::amount::underlying_type>::max ()));
}

TEST (bucketing, indices)
{
	celerix::bucketing bucketing;
	auto indices = bucketing.bucket_indices ();
	ASSERT_EQ (63, indices.size ());
	ASSERT_EQ (indices.size (), bucketing.size ());

	// Check that the indices are in ascending order
	ASSERT_TRUE (std::adjacent_find (indices.begin (), indices.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs >= rhs;
	})
	== indices.end ());
}