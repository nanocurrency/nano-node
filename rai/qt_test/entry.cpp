#include <gtest/gtest.h>

#include <QApplication>

QApplication * test_application = nullptr;

int main (int argc, char ** argv)
{
	QApplication application (argc, argv);
	test_application = &application;
	testing::InitGoogleTest (&argc, argv);
	return RUN_ALL_TESTS ();
}
