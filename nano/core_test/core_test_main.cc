#include "gtest/gtest.h"

#include <nano/lib/logging.hpp>
#include <nano/lib/stacktrace.hpp>
#include <nano/node/common.hpp>
#include <nano/secure/utility.hpp>

#include <boost/filesystem/path.hpp>

#include <stdlib.h>
#include <signal.h>

constexpr std::size_t OPEN_FILE_DESCRIPTORS_LIMIT = 16384;

namespace nano
{
namespace test
{
	void cleanup_dev_directories_on_exit ();
}
void force_nano_dev_network ();
}

void signalHandler(int signum)
{
	std::cerr << "SIGSEGV signal handler\n";
	std::cerr << nano::generate_stacktrace () << std::endl;
	exit(signum);
}

GTEST_API_ int main (int argc, char ** argv)
{
	signal(SIGSEGV, signalHandler);
	nano::logger::initialize_for_tests (nano::log_config::tests_default ());
	nano::set_file_descriptor_limit (OPEN_FILE_DESCRIPTORS_LIMIT);
	nano::force_nano_dev_network ();
	nano::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	nano::test::cleanup_dev_directories_on_exit ();
	return res;
}
