#include <gtest/gtest.h>

namespace nano
{
void force_nano_test_network ();
}

int main (int argc, char ** argv)
{
	nano::force_nano_test_network ();
	testing::InitGoogleTest (&argc, argv);
	return RUN_ALL_TESTS ();
}
