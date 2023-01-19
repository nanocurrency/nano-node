/**
 * IMPORTANT NOTE:
 * These unit tests may or may not work, gtest and boost asio signal handling are not really compatible.
 * The boost asio signal handling assumes that it is the only one handling signals but gtest
 * also does some signal handling of its own. In my testing this setup works although in theory
 * I am playing with unspecified behaviour. If these tests start causing problems then we should
 * remove them and try some other approach.
 * The tests are designed as death tests because, as normal tests, the boost library asserts
 * when I define more than one test case. I have not investigated why, I just turned them into death tests.
 *
 * Update: it appears that these tests only work if run in isolation so I am disabling them.
 */

#include <nano/lib/signal_manager.hpp>

#include <gtest/gtest.h>

#include <boost/format.hpp>

#include <csignal>
#include <iostream>
#include <thread>

static void handler_print_signal (int signum)
{
	std::cerr << "boost signal handler " << signum << std::endl
			  << std::flush;
}

static int wait_for_sig_received (int millisecs, int & sig_received)
{
	for (int i = 0; i < millisecs && sig_received == 0; i++)
	{
		std::this_thread::sleep_for (std::chrono::microseconds (1));
	}
	return sig_received;
}

static int trap (int signum)
{
	nano::signal_manager sigman;
	int sig_received = 0;

	std::function<void (int)> f = [&sig_received] (int signum) {
		std::cerr << "boost signal handler " << signum << std::endl
				  << std::flush;
		sig_received = signum;
	};

	sigman.register_signal_handler (signum, f, false);

	raise (signum);

	exit (wait_for_sig_received (10000, sig_received));
}

static void repeattest (int signum, bool repeat)
{
	nano::signal_manager sigman;
	int sig_received = 0;

	std::function<void (int)> f = [&sig_received] (int signum) {
		std::cerr << "boost signal handler" << std::flush;
		sig_received = signum;
	};

	sigman.register_signal_handler (signum, f, repeat);

	for (int i = 0; i < 10; i++)
	{
		sig_received = 0;
		raise (signum);
		if (wait_for_sig_received (10000, sig_received) != signum)
		{
			exit (1);
		}
	}

	exit (0);
}

TEST (DISABLED_signal_manager_test, trap)
{
	int signum;

	signum = SIGINT;
	ASSERT_EXIT (trap (signum), ::testing::ExitedWithCode (signum), "");

	signum = SIGTERM;
	ASSERT_EXIT (trap (signum), ::testing::ExitedWithCode (signum), "");
}

TEST (DISABLED_signal_manager_test, repeat)
{
	int signum;

	signum = SIGINT;
	ASSERT_EXIT (repeattest (signum, true), ::testing::ExitedWithCode (0), "");
}

TEST (DISABLED_signal_manager_test, norepeat)
{
	int signum;

	signum = SIGINT;
	ASSERT_DEATH (repeattest (signum, false), "^boost signal handler$");
}
