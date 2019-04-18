#include "gtest/gtest.h"
#include <nano/secure/utility.hpp>
namespace nano
{
void force_nano_test_network ();
}
GTEST_API_ int main (int argc, char ** argv)
{
	printf ("Running main() from core_test_main.cc\n");
	nano::force_nano_test_network ();
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	nano::cleanp_test_directories_on_exit ();
	return res;
}
