#include <nano/lib/logging.hpp>
#include <nano/lib/memory.hpp>
#include <nano/node/common.hpp>

#include <gtest/gtest.h>

namespace nano
{
namespace test
{
	void cleanup_dev_directories_on_exit ();
}
void force_nano_dev_network ();
}

int main (int argc, char ** argv)
{
	nano::initialize_file_descriptor_limit ();
	nano::logger::initialize_for_tests (nano::log_config::tests_default ());
	nano::force_nano_dev_network ();
	nano::set_use_memory_pools (false);
	nano::node_singleton_memory_pool_purge_guard cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	nano::test::cleanup_dev_directories_on_exit ();
	return res;
}
