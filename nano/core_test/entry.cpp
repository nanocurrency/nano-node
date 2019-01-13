#include <gtest/gtest.h>

TEST (basic, basic)
{
	ASSERT_TRUE (true);
}

TEST (asan, DISABLED_memory)
{
	uint8_t array[1];
	auto value (array[-0x800000]);
}