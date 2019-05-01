#pragma once

#include <boost/property_tree/ptree.hpp>
#include <boost/stacktrace/stacktrace_fwd.hpp>
#include <mutex>
#include <nano/lib/timer.hpp>

namespace nano
{
class transaction_impl;
class logger_mt;

class mdb_txn_stats
{
public:
	mdb_txn_stats (nano::transaction_impl * transaction_impl_a, bool is_write_a);
	nano::timer<std::chrono::seconds> timer;
	nano::transaction_impl * transaction_impl;
	std::string thread_name;

	// Smart pointer so that we don't need the full definition which causes min/max issues on Windows
	std::shared_ptr<boost::stacktrace::stacktrace> stacktrace;
	bool is_write;
};

class mdb_txn_tracker
{
public:
	mdb_txn_tracker (nano::logger_mt & logger_a, bool is_logging_database_locking_a);
	void serialize_json (boost::property_tree::ptree & json, std::chrono::seconds min_time);
	void add (nano::transaction_impl * transaction_impl, bool is_write_a);
	void erase (nano::transaction_impl * transaction_impl);

private:
	std::mutex mutex;
	std::vector<mdb_txn_stats> stats;
	nano::logger_mt & logger;
	bool is_logging_database_locking;
	void output_finished (nano::mdb_txn_stats & mdb_txn_stats);
	static std::chrono::seconds constexpr min_time_locked_ouput{ 10 };
};
}
