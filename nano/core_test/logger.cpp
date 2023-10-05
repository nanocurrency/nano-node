#include <nano/lib/logger_mt.hpp>
#include <nano/node/logging.hpp>
#include <nano/secure/utility.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include <chrono>
#include <regex>
#include <thread>

using namespace std::chrono_literals;

TEST (logger, changing_time_interval)
{
	auto path1 (nano::unique_path ());
	nano::logging logging;
	logging.init (path1);
	logging.min_time_between_log_output = 0ms;
	nano::logger_mt my_logger (logging.min_time_between_log_output);
	auto error (my_logger.try_log ("logger.changing_time_interval1"));
	ASSERT_FALSE (error);
	my_logger.min_log_delta = 20s;
	error = my_logger.try_log ("logger, changing_time_interval2");
	ASSERT_TRUE (error);
}

TEST (logger, try_log)
{
	auto path1 (nano::unique_path ());
	std::stringstream ss;
	nano::test::boost_log_cerr_redirect redirect_cerr (ss.rdbuf ());
	nano::logger_mt my_logger (100ms);
	auto output1 = "logger.try_log1";
	auto error (my_logger.try_log (output1));
	ASSERT_FALSE (error);
	auto output2 = "logger.try_log2";
	error = my_logger.try_log (output2);
	ASSERT_TRUE (error); // Fails as it is occuring too soon

	// Sleep for a bit and then confirm
	std::this_thread::sleep_for (100ms);
	error = my_logger.try_log (output2);
	ASSERT_FALSE (error);

	std::string str;
	std::getline (ss, str, '\n');
	ASSERT_STREQ (str.c_str (), output1);
	std::getline (ss, str, '\n');
	ASSERT_STREQ (str.c_str (), output2);
}

TEST (logger, always_log)
{
	auto path1 (nano::unique_path ());
	std::stringstream ss;
	nano::test::boost_log_cerr_redirect redirect_cerr (ss.rdbuf ());
	nano::logger_mt my_logger (20s); // Make time interval effectively unreachable
	auto output1 = "logger.always_log1";
	auto error (my_logger.try_log (output1));
	ASSERT_FALSE (error);

	// Time is too soon after, so it won't be logged
	auto output2 = "logger.always_log2";
	error = my_logger.try_log (output2);
	ASSERT_TRUE (error);

	// Force it to be logged
	my_logger.always_log (output2);

	std::string str;
	std::getline (ss, str, '\n');
	ASSERT_STREQ (str.c_str (), output1);
	std::getline (ss, str, '\n');
	ASSERT_STREQ (str.c_str (), output2);
}

TEST (logger, stable_filename)
{
	auto path (nano::unique_path ());
	nano::logging logging;

	// Releasing allows setting up logging again
	logging.release_file_sink ();

	logging.stable_log_filename = true;
	logging.init (path);

	nano::logger_mt logger (logging.min_time_between_log_output);
	logger.always_log ("stable1");

	auto log_file = path / "log" / "node.log";

#if BOOST_VERSION >= 107000
	EXPECT_TRUE (std::filesystem::exists (log_file));
	// Try opening it again
	logging.release_file_sink ();
	logging.init (path);
	logger.always_log ("stable2");
#else
	// When using Boost < 1.70 , behavior is reverted to not using the stable filename
	EXPECT_FALSE (std::filesystem::exists (log_file));
#endif

	// Reset the logger
	logging.release_file_sink ();
	nano::logging ().init (path);
}
