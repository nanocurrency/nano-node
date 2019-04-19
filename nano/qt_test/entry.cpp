#include <QApplication>
#include <gtest/gtest.h>
#include <nano/secure/utility.hpp>
QApplication * test_application = nullptr;
namespace nano
{
void force_nano_test_network ();
}

int main (int argc, char ** argv)
{
	nano::force_nano_test_network ();
	QApplication application (argc, argv);
	test_application = &application;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	nano::cleanp_test_directories_on_exit ();
	return res;
}
