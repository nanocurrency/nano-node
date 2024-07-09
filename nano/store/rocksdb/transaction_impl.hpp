#pragma once

#include <nano/store/component.hpp>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/utilities/transaction_db.h>

namespace nano::store::rocksdb
{
class read_transaction_impl final : public store::read_transaction_impl
{
public:
	read_transaction_impl (::rocksdb::DB * db);
	~read_transaction_impl ();
	void reset () override;
	void renew () override;
	void * get_handle () const override;

private:
	::rocksdb::DB * db;
	::rocksdb::ReadOptions options;
};

class write_transaction_impl final : public store::write_transaction_impl
{
public:
	write_transaction_impl (::rocksdb::TransactionDB * db_a, std::vector<nano::tables> const & tables_requiring_locks_a, std::vector<nano::tables> const & tables_no_locks_a, std::unordered_map<nano::tables, nano::mutex> & mutexes_a);
	~write_transaction_impl ();
	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (nano::tables table_a) const override;

private:
	::rocksdb::Transaction * txn;
	::rocksdb::TransactionDB * db;
	std::vector<nano::tables> tables_requiring_locks;
	std::vector<nano::tables> tables_no_locks;
	std::unordered_map<nano::tables, nano::mutex> & mutexes;
	bool active{ true };

	void lock ();
	void unlock ();
};
}
