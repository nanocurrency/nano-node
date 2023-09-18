#pragma once

#include <nano/store/component.hpp>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace nano
{
class read_rocksdb_txn final : public read_transaction_impl
{
public:
	read_rocksdb_txn (::rocksdb::DB * db);
	~read_rocksdb_txn ();
	void reset () override;
	void renew () override;
	void * get_handle () const override;

private:
	::rocksdb::DB * db;
	::rocksdb::ReadOptions options;
};

class write_rocksdb_txn final : public write_transaction_impl
{
public:
	write_rocksdb_txn (::rocksdb::OptimisticTransactionDB * db_a, std::vector<nano::tables> const & tables_requiring_locks_a, std::vector<nano::tables> const & tables_no_locks_a, std::unordered_map<nano::tables, nano::mutex> & mutexes_a);
	~write_rocksdb_txn ();
	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (nano::tables table_a) const override;

private:
	::rocksdb::Transaction * txn;
	::rocksdb::OptimisticTransactionDB * db;
	std::vector<nano::tables> tables_requiring_locks;
	std::vector<nano::tables> tables_no_locks;
	std::unordered_map<nano::tables, nano::mutex> & mutexes;
	bool active{ true };

	void lock ();
	void unlock ();
};
}
