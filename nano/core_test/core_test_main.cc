#include "gtest/gtest.h"

#include <nano/lib/memory.hpp>
#include <nano/node/common.hpp>

namespace nano
{
void cleanup_test_directories_on_exit ();
void force_nano_test_network ();
}

GTEST_API_ int main (int argc, char ** argv)
{
	printf ("Running main() from core_test_main.cc\n");
	nano::force_nano_test_network ();
#ifdef __APPLE__
	// TSAN can generate false-positives in shared/weak_ptr destructors
	nano::use_memory_pools = !is_thread_sanitizer_build;
#else
	nano::use_memory_pools = true;
#endif
	nano::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	nano::cleanup_test_directories_on_exit ();
	return res;
}
