#pragma once

#include <nano/lib/diagnosticsconfig.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/timer.hpp>
#include <nano/store/transaction.hpp>

#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/stacktrace/stacktrace_fwd.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

namespace nano::store
{
/**
 * Callback structure for transaction lifecycle events.
 * Backend-agnostic - can be used by any database backend.
 */
class txn_callbacks
{
public:
	std::function<void (transaction_impl const *)> txn_start{ [] (transaction_impl const *) {} };
	std::function<void (transaction_impl const *)> txn_end{ [] (transaction_impl const *) {} };
};

/**
 * Statistics for a single transaction.
 * Captures timing, thread info, and stacktrace at transaction start.
 */
class txn_stats
{
public:
	explicit txn_stats (transaction_impl const * txn_impl);
	bool is_write () const;

	nano::timer<std::chrono::milliseconds> timer;
	transaction_impl const * txn_impl;
	std::string thread_name;

	// Smart pointer so that we don't need the full definition which causes min/max issues on Windows
	std::shared_ptr<boost::stacktrace::stacktrace> stacktrace;
};

/**
 * Tracks active transactions and logs those held too long.
 * Backend-agnostic implementation.
 */
class txn_tracker
{
public:
	txn_tracker (nano::logger &, nano::txn_tracking_config const & txn_tracking_config);

	void serialize_json (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time);
	void add (transaction_impl const * transaction_impl);
	void erase (transaction_impl const * transaction_impl);

private:
	nano::mutex mutex;
	std::vector<txn_stats> stats;
	nano::logger & logger;
	nano::txn_tracking_config txn_tracking_config;

	void log_if_held_long_enough (txn_stats const & stats) const;
};
}
