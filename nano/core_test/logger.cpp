#include <nano/core_test/testutil.hpp>
#include <nano/node/logging.hpp>
#include <nano/secure/utility.hpp>

#include <gtest/gtest.h>

#include <boost/log/utility/setup/console.hpp>

#include <chrono>
#include <regex>

using namespace std::chrono_literals;

TEST (logging, serialization)
{
	auto path (nano::unique_path ());
	nano::logging logging1;
	logging1.init (path);
	logging1.ledger_logging_value = !logging1.ledger_logging_value;
	logging1.ledger_duplicate_logging_value = !logging1.ledger_duplicate_logging_value;
	logging1.network_logging_value = !logging1.network_logging_value;
	logging1.network_timeout_logging_value = !logging1.network_timeout_logging_value;
	logging1.network_message_logging_value = !logging1.network_message_logging_value;
	logging1.network_publish_logging_value = !logging1.network_publish_logging_value;
	logging1.network_packet_logging_value = !logging1.network_packet_logging_value;
	logging1.network_keepalive_logging_value = !logging1.network_keepalive_logging_value;
	logging1.network_node_id_handshake_logging_value = !logging1.network_node_id_handshake_logging_value;
	logging1.node_lifetime_tracing_value = !logging1.node_lifetime_tracing_value;
	logging1.insufficient_work_logging_value = !logging1.insufficient_work_logging_value;
	logging1.bulk_pull_logging_value = !logging1.bulk_pull_logging_value;
	logging1.work_generation_time_value = !logging1.work_generation_time_value;
	logging1.log_to_cerr_value = !logging1.log_to_cerr_value;
	logging1.max_size = 10;
	logging1.min_time_between_log_output = 100ms;
	nano::jsonconfig tree;
	logging1.serialize_json (tree);
	nano::logging logging2;
	logging2.init (path);
	bool upgraded (false);
	ASSERT_FALSE (logging2.deserialize_json (upgraded, tree));
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (logging1.ledger_logging_value, logging2.ledger_logging_value);
	ASSERT_EQ (logging1.ledger_duplicate_logging_value, logging2.ledger_duplicate_logging_value);
	ASSERT_EQ (logging1.network_logging_value, logging2.network_logging_value);
	ASSERT_EQ (logging1.network_timeout_logging_value, logging2.network_timeout_logging_value);
	ASSERT_EQ (logging1.network_message_logging_value, logging2.network_message_logging_value);
	ASSERT_EQ (logging1.network_publish_logging_value, logging2.network_publish_logging_value);
	ASSERT_EQ (logging1.network_packet_logging_value, logging2.network_packet_logging_value);
	ASSERT_EQ (logging1.network_keepalive_logging_value, logging2.network_keepalive_logging_value);
	ASSERT_EQ (logging1.network_node_id_handshake_logging_value, logging2.network_node_id_handshake_logging_value);
	ASSERT_EQ (logging1.node_lifetime_tracing_value, logging2.node_lifetime_tracing_value);
	ASSERT_EQ (logging1.insufficient_work_logging_value, logging2.insufficient_work_logging_value);
	ASSERT_EQ (logging1.bulk_pull_logging_value, logging2.bulk_pull_logging_value);
	ASSERT_EQ (logging1.work_generation_time_value, logging2.work_generation_time_value);
	ASSERT_EQ (logging1.log_to_cerr_value, logging2.log_to_cerr_value);
	ASSERT_EQ (logging1.max_size, logging2.max_size);
	ASSERT_EQ (logging1.min_time_between_log_output, logging2.min_time_between_log_output);
}

TEST (logging, upgrade_v1_v2)
{
	auto path1 (nano::unique_path ());
	auto path2 (nano::unique_path ());
	nano::logging logging1;
	logging1.init (path1);
	nano::logging logging2;
	logging2.init (path2);
	nano::jsonconfig tree;
	logging1.serialize_json (tree);
	tree.erase ("version");
	tree.erase ("vote");
	bool upgraded (false);
	ASSERT_FALSE (logging2.deserialize_json (upgraded, tree));
	ASSERT_LE (2, tree.get<int> ("version"));
	ASSERT_FALSE (tree.get<bool> ("vote"));
}

TEST (logging, upgrade_v6_v7)
{
	auto path1 (nano::unique_path ());
	auto path2 (nano::unique_path ());
	nano::logging logging1;
	logging1.init (path1);
	nano::logging logging2;
	logging2.init (path2);
	nano::jsonconfig tree;
	logging1.serialize_json (tree);
	tree.erase ("version");
	tree.erase ("min_time_between_output");
	tree.erase ("network_timeout_logging_value");
	bool upgraded (false);
	ASSERT_FALSE (logging2.deserialize_json (upgraded, tree));
	ASSERT_TRUE (upgraded);
	ASSERT_LE (7, tree.get<int> ("version"));
	ASSERT_EQ (5, tree.get<uintmax_t> ("min_time_between_output"));
	ASSERT_EQ (false, tree.get<bool> ("network_timeout_logging_value"));
}

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
	nano::boost_log_cerr_redirect redirect_cerr (ss.rdbuf ());
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
	nano::boost_log_cerr_redirect redirect_cerr (ss.rdbuf ());
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
