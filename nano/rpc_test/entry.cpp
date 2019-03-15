#include <gtest/gtest.h>

extern void force_nano_test_network ();

int main (int argc, char ** argv)
{
	force_nano_test_network ();
	testing::InitGoogleTest (&argc, argv);
	return RUN_ALL_TESTS ();
}
