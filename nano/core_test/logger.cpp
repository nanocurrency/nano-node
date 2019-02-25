#include <chrono>
#include <gtest/gtest.h>
#include <nano/node/logging.hpp>
#include <nano/secure/utility.hpp>
#include <regex>

using namespace std::chrono_literals;

namespace
{
std::string strip_timestamp (const std::string & line);
std::vector<std::string> get_last_lines_from_log_file (const std::string & log_path, unsigned count);
}

TEST (logging, serialization)
{
	auto path (nano::unique_path ());
	nano::logging logging1;
	logging1.init (path);
	logging1.ledger_logging_value = !logging1.ledger_logging_value;
	logging1.ledger_duplicate_logging_value = !logging1.ledger_duplicate_logging_value;
	logging1.network_logging_value = !logging1.network_logging_value;
	logging1.network_message_logging_value = !logging1.network_message_logging_value;
	logging1.network_publish_logging_value = !logging1.network_publish_logging_value;
	logging1.network_packet_logging_value = !logging1.network_packet_logging_value;
	logging1.network_keepalive_logging_value = !logging1.network_keepalive_logging_value;
	logging1.network_node_id_handshake_logging_value = !logging1.network_node_id_handshake_logging_value;
	logging1.node_lifetime_tracing_value = !logging1.node_lifetime_tracing_value;
	logging1.insufficient_work_logging_value = !logging1.insufficient_work_logging_value;
	logging1.log_rpc_value = !logging1.log_rpc_value;
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
	ASSERT_EQ (logging1.network_message_logging_value, logging2.network_message_logging_value);
	ASSERT_EQ (logging1.network_publish_logging_value, logging2.network_publish_logging_value);
	ASSERT_EQ (logging1.network_packet_logging_value, logging2.network_packet_logging_value);
	ASSERT_EQ (logging1.network_keepalive_logging_value, logging2.network_keepalive_logging_value);
	ASSERT_EQ (logging1.network_node_id_handshake_logging_value, logging2.network_node_id_handshake_logging_value);
	ASSERT_EQ (logging1.node_lifetime_tracing_value, logging2.node_lifetime_tracing_value);
	ASSERT_EQ (logging1.insufficient_work_logging_value, logging2.insufficient_work_logging_value);
	ASSERT_EQ (logging1.log_rpc_value, logging2.log_rpc_value);
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
	bool upgraded (false);
	ASSERT_FALSE (logging2.deserialize_json (upgraded, tree));
	ASSERT_TRUE (upgraded);
	ASSERT_LE (7, tree.get<int> ("version"));
	ASSERT_EQ (5, tree.get<uintmax_t> ("min_time_between_output"));
}

TEST (logger, changing_time_interval)
{
	auto path1 (nano::unique_path ());
	nano::logging logging;
	logging.init (path1);
	logging.min_time_between_log_output = 0ms;
	nano::logger_mt my_logger (logging.min_time_between_log_output);
	auto log_path = logging.log_path ();
	auto success (my_logger.try_log ("logger.changing_time_interval1"));
	ASSERT_TRUE (success);
	logging.min_time_between_log_output = 20s;
	success = my_logger.try_log ("logger, changing_time_interval2");
	ASSERT_FALSE (success);
}

TEST (logger, try_log)
{
	auto path1 (nano::unique_path ());
	nano::logging logging;
	logging.init (path1);
	nano::logger_mt my_logger (3ms);

	auto output1 = "logger.try_log1";
	auto output2 = "logger.try_log2";

	auto success (my_logger.try_log (output1));
	ASSERT_TRUE (success);
	success = my_logger.try_log (output2);
	ASSERT_FALSE (success); // Fails as it is occuring too soon

	// Sleep for a bit and then confirm
	std::this_thread::sleep_for (3ms);
	success = my_logger.try_log (output2);
	ASSERT_TRUE (success);

	auto log_path = logging.log_path ();
	auto last_lines = get_last_lines_from_log_file (log_path, 2);
	// Remove the timestamp from the line in the log file to make comparisons timestamp independent
	ASSERT_STREQ (strip_timestamp (last_lines.front ()).c_str (), output1);
	ASSERT_STREQ (strip_timestamp (last_lines.back ()).c_str (), output2);
}

TEST (logger, always_log)
{
	auto path1 (nano::unique_path ());
	nano::logging logging;
	logging.init (path1);
	nano::logger_mt my_logger (20s); // Make time interval effectively unreachable
	auto output1 = "logger.always_log1";
	auto success (my_logger.try_log (output1));
	ASSERT_TRUE (success);

	// Time is too soon after, so it won't be logged
	auto output2 = "logger.always_log2";
	success = my_logger.try_log (output2);
	ASSERT_FALSE (success);

	// Force it to be logged
	my_logger.always_log (output2);

	// Now check
	auto log_path = logging.log_path ();
	auto last_lines = get_last_lines_from_log_file (log_path, 2);
	ASSERT_STREQ (strip_timestamp (last_lines.front ()).c_str (), output1);
	ASSERT_STREQ (strip_timestamp (last_lines.back ()).c_str (), output2);
}

namespace
{
std::string strip_timestamp (const std::string & line)
{
	std::regex line_regex (".+\\]: (.+)");
	return std::regex_replace (line, line_regex, "$1");
}

std::vector<std::string> get_last_lines_from_log_file (const std::string & log_path, unsigned count)
{
	assert (count > 0);
	boost::filesystem::directory_iterator it{ log_path };

	auto log_file = it->path ();
	std::ifstream in_file (log_file.c_str ());

	std::vector<std::string> v (count);
	for (std::string line; std::getline (in_file, line);)
	{
		std::string next_line;
		if (!std::getline (in_file, next_line))
		{
			// Already reached last line so shift others to the left and update last line
			std::rotate (v.begin (), v.begin () + 1, v.end ());
			v.back () = std::move (line);
		}
		else
		{
			if (count == 1)
			{
				v.front () = next_line;
			}
			else if (count > 1)
			{
				v.front () = line;
				v[1] = next_line;

				for (unsigned i = 0; i < count - 2; ++i)
				{
					std::getline (in_file, v[i + 2]);
				}
			}
		}
	}
	return v;
}
}
