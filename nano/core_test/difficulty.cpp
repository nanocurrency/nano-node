#include <gtest/gtest.h>

#include <nano/lib/numbers.hpp>

TEST (difficulty, multipliers)
{
	{
		uint64_t base = 0xff00000000000000;
		uint64_t difficulty = 0xfff27e7a57c285cd;
		double expected_multiplier = 18.95461493377003;

		ASSERT_NEAR (expected_multiplier, nano::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, nano::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
		uint64_t base = 0xffffffc000000000;
		uint64_t difficulty = 0xfffffe0000000000;
		double expected_multiplier = 0.125;

		auto multiplier = nano::difficulty::to_multiplier (difficulty, base);
		ASSERT_NEAR (expected_multiplier, nano::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, nano::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
		uint64_t base = 0xffffffc000000000;
		uint64_t difficulty_nil = 0;
		uint64_t multiplier_nil = 0.;
		ASSERT_DEATH (nano::difficulty::to_multiplier (difficulty_nil, base), "");
		ASSERT_DEATH (nano::difficulty::from_multiplier (multiplier_nil, base), "");
	}
}