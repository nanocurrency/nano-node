#include "gtest/gtest.h"
namespace nano
{
void force_nano_test_network ();
}
GTEST_API_ int main (int argc, char ** argv)
{
	printf ("Running main() from core_test_main.cc\n");
	nano::force_nano_test_network ();
	testing::InitGoogleTest (&argc, argv);
	return RUN_ALL_TESTS ();
}
