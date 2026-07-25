#include <nano/lib/assert.hpp>

#include <gtest/gtest.h>

TEST (assert_DeathTest, debug_assert)
{
	debug_assert (true);
	ASSERT_DEATH (debug_assert (false, "test"), ".*Assertion `false` failed: test.*");
}

TEST (assert_DeathTest, release_assert)
{
	release_assert (true);
	ASSERT_DEATH (release_assert (false, "test"), ".*Assertion `false` failed: test.*");
}

/*
 * Message arguments must only be evaluated when the check fails
 */
TEST (assert, lazy_arguments)
{
	int evaluations = 0;
	auto extra = [&evaluations] {
		++evaluations;
		return "extra";
	};

	release_assert (true, "message", extra ());
	debug_assert (true, "message", extra ());
	ASSERT_EQ (0, evaluations);
}