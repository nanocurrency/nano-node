#include <nano/lib/memory.hpp>
#include <nano/node/common.hpp>

#include <gtest/gtest.h>
namespace nano
{
void cleanup_test_directories_on_exit ();
void force_nano_test_network ();
}

int main (int argc, char ** argv)
{
	nano::force_nano_test_network ();
	nano::set_use_memory_pools (false);
	nano::node_singleton_memory_pool_purge_guard cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	nano::cleanup_test_directories_on_exit ();
	return res;
}
