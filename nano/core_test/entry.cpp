#include <nano/lib/logging.hpp>
#include <nano/node/common.hpp>
#include <nano/secure/utility.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem/path.hpp>

namespace nano
{
namespace test
{
	void cleanup_dev_directories_on_exit ();
}
void force_nano_dev_network ();
}

GTEST_API_ int main (int argc, char ** argv)
{
	nano::initialize_file_descriptor_limit ();
	nano::logger::initialize_for_tests (nano::log_config::tests_default ());
	nano::force_nano_dev_network ();
	nano::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	nano::test::cleanup_dev_directories_on_exit ();
	return res;
}
