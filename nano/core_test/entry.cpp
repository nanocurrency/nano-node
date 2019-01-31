#include <gtest/gtest.h>

TEST (basic, basic)
{
	ASSERT_TRUE (true);
}

TEST (asan, DISABLED_memory)
{
#pragma warning (push)
#pragma warning (disable : 4045 )
    uint8_t array[1];
    (void)array;
    
    // Commented out to remove out of index array
	//auto value (array[-0x800000]);
#pragma warning (pop)
}
