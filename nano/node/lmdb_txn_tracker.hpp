#pragma once

#include <nano/lib/timer.hpp>
#include <nano/node/diagnosticsconfig.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/stacktrace/stacktrace_fwd.hpp>

#include <mutex>

namespace nano
{
class transaction_impl;
class logger_mt;

class mdb_txn_stats
{
public:
	mdb_txn_stats (const nano::transaction_impl * transaction_impl_a);
	bool is_write () const;
	nano::timer<std::chrono::milliseconds> timer;
	const nano::transaction_impl * transaction_impl;
	std::string thread_name;

	// Smart pointer so that we don't need the full definition which causes min/max issues on Windows
	std::shared_ptr<boost::stacktrace::stacktrace> stacktrace;
};

class mdb_txn_tracker
{
public:
	mdb_txn_tracker (nano::logger_mt & logger_a, nano::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a);
	void serialize_json (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time);
	void add (const nano::transaction_impl * transaction_impl);
	void erase (const nano::transaction_impl * transaction_impl);

private:
	std::mutex mutex;
	std::vector<mdb_txn_stats> stats;
	nano::logger_mt & logger;
	nano::txn_tracking_config txn_tracking_config;
	std::chrono::milliseconds block_processor_batch_max_time;

	void output_finished (nano::mdb_txn_stats const & mdb_txn_stats) const;
};
}
