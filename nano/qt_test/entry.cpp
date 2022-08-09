#include <nano/node/common.hpp>

#include <gtest/gtest.h>

#include <QApplication>
QApplication * test_application = nullptr;
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
	nano::force_nano_dev_network ();
	nano::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	QApplication application (argc, argv);
	test_application = &application;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	nano::test::cleanup_dev_directories_on_exit ();
	return res;
}
