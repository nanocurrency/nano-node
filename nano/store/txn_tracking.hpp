#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/fwd.hpp>
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
 * Configuration for transaction tracking.
 */
class txn_tracking_config final
{
public:
	nano::error serialize_toml (nano::tomlconfig &) const;
	nano::error deserialize_toml (nano::tomlconfig &);

public:
	/** If true, enable tracking for transaction read/writes held open longer than the min time variables */
	bool enable{ false };
	std::chrono::milliseconds min_read_txn_time{ 5000 };
	std::chrono::milliseconds min_write_txn_time{ 500 };
};

/**
 * Callback structure for transaction lifecycle events.
 */
class txn_callbacks
{
public:
	std::function<void (nano::store::transaction_impl const *)> txn_start{ [] (nano::store::transaction_impl const *) {} };
	std::function<void (nano::store::transaction_impl const *)> txn_end{ [] (nano::store::transaction_impl const *) {} };
};

/**
 * Statistics for a single transaction.
 * Captures timing, thread info, and stacktrace at transaction start.
 */
class txn_stats
{
public:
	explicit txn_stats (nano::store::transaction_impl const * txn_impl);
	bool is_write () const;

public:
	nano::timer<std::chrono::milliseconds> timer;
	nano::store::transaction_impl const * txn_impl;
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
	txn_tracker (nano::logger &, txn_tracking_config const & txn_tracking_config);

	void serialize_json (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time);
	void add (transaction_impl const * transaction_impl);
	void erase (transaction_impl const * transaction_impl);

private:
	nano::mutex mutex;
	std::vector<txn_stats> stats;
	nano::logger & logger;
	txn_tracking_config config;

	void log_if_held_long_enough (txn_stats const & stats) const;
};
}
