#include <celerix/lib/files.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/memory.hpp>

#include <gtest/gtest.h>

namespace celerix
{
namespace test
{
	void cleanup_dev_directories_on_exit ();
}
void force_celerix_dev_network ();
}

int main (int argc, char ** argv)
{
	celerix::initialize_file_descriptor_limit ();
	celerix::logger::initialize_for_tests (celerix::log_config::tests_default ());
	celerix::force_celerix_dev_network ();
	celerix::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	celerix::test::cleanup_dev_directories_on_exit ();
	return res;
}
