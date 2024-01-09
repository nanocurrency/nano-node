#pragma once

#include <nano/lib/diagnosticsconfig.hpp>
#include <nano/lib/id_dispenser.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/timer.hpp>
#include <nano/store/component.hpp>
#include <nano/store/transaction.hpp>

#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/stacktrace/stacktrace_fwd.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
class env;
}

namespace nano::store::lmdb
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
	read_transaction_impl (nano::store::lmdb::env const &, txn_callbacks mdb_txn_callbacks);
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
	write_transaction_impl (nano::store::lmdb::env const &, txn_callbacks mdb_txn_callbacks);
	~write_transaction_impl ();
	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (nano::tables table_a) const override;
	MDB_txn * handle;
	nano::store::lmdb::env const & env;
	lmdb::txn_callbacks txn_callbacks;
	bool active{ true };
};
} // namespace nano::store::lmdb

namespace nano
{
class mdb_txn_stats
{
public:
	mdb_txn_stats (store::transaction_impl const * transaction_impl_a);
	bool is_write () const;
	nano::timer<std::chrono::milliseconds> timer;
	store::transaction_impl const * transaction_impl;
	std::string thread_name;

	// Smart pointer so that we don't need the full definition which causes min/max issues on Windows
	std::shared_ptr<boost::stacktrace::stacktrace> stacktrace;
};

class mdb_txn_tracker
{
public:
	mdb_txn_tracker (nano::nlogger &, nano::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a);
	void serialize_json (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time);
	void add (store::transaction_impl const * transaction_impl);
	void erase (store::transaction_impl const * transaction_impl);

private:
	nano::mutex mutex;
	std::vector<mdb_txn_stats> stats;
	nano::nlogger & nlogger;
	nano::txn_tracking_config txn_tracking_config;
	std::chrono::milliseconds block_processor_batch_max_time;

	void log_if_held_long_enough (nano::mdb_txn_stats const & mdb_txn_stats) const;
};
} // namespace nano
