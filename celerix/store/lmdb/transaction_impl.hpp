#pragma once

#include <celerix/lib/diagnosticsconfig.hpp>
#include <celerix/lib/id_dispenser.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/transaction.hpp>

#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/stacktrace/stacktrace_fwd.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix::store::lmdb
{
class env;
}

namespace celerix::store::lmdb
{
class txn_callbacks
{
public:
	std::function<void (store::transaction_impl const *)> txn_start{ [] (store::transaction_impl const *) {} };
	std::function<void (store::transaction_impl const *)> txn_end{ [] (store::transaction_impl const *) {} };
};

class read_transaction_impl final : public store::read_transaction_impl
{
public:
	read_transaction_impl (celerix::store::lmdb::env const &, txn_callbacks mdb_txn_callbacks);
	~read_transaction_impl ();
	void reset () override;
	void renew () override;
	void * get_handle () const override;
	MDB_txn * handle;
	lmdb::txn_callbacks txn_callbacks;
};

class write_transaction_impl final : public store::write_transaction_impl
{
public:
	write_transaction_impl (celerix::store::lmdb::env const &, txn_callbacks mdb_txn_callbacks);
	~write_transaction_impl ();
	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (celerix::tables table_a) const override;
	MDB_txn * handle;
	celerix::store::lmdb::env const & env;
	lmdb::txn_callbacks txn_callbacks;
	bool active{ true };
};
} // namespace celerix::store::lmdb

namespace celerix
{
class mdb_txn_stats
{
public:
	mdb_txn_stats (store::transaction_impl const * transaction_impl_a);
	bool is_write () const;
	celerix::timer<std::chrono::milliseconds> timer;
	store::transaction_impl const * transaction_impl;
	std::string thread_name;

	// Smart pointer so that we don't need the full definition which causes min/max issues on Windows
	std::shared_ptr<boost::stacktrace::stacktrace> stacktrace;
};

class mdb_txn_tracker
{
public:
	mdb_txn_tracker (celerix::logger &, celerix::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a);
	void serialize_json (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time);
	void add (store::transaction_impl const * transaction_impl);
	void erase (store::transaction_impl const * transaction_impl);

private:
	celerix::mutex mutex;
	std::vector<mdb_txn_stats> stats;
	celerix::logger & logger;
	celerix::txn_tracking_config txn_tracking_config;
	std::chrono::milliseconds block_processor_batch_max_time;

	void log_if_held_long_enough (celerix::mdb_txn_stats const & mdb_txn_stats) const;
};
} // namespace celerix
