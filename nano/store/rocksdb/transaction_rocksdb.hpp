#pragma once

#include <nano/store/transaction.hpp>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/utilities/transaction_db.h>

namespace nano::store::rocksdb
{
class read_transaction_impl final : public nano::store::read_transaction_impl
{
public:
	explicit read_transaction_impl (::rocksdb::DB * db);
	~read_transaction_impl () override;

	void reset () override;
	void renew () override;
	void * get_handle () const override;

private:
	::rocksdb::DB * db;
	::rocksdb::ReadOptions options{};
	bool active{ false };
};

class write_transaction_impl final : public nano::store::write_transaction_impl
{
public:
	explicit write_transaction_impl (::rocksdb::TransactionDB * db_a);
	~write_transaction_impl () override;

	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (nano::store::table) const override;

private:
	bool check_no_write_tx () const;

	::rocksdb::TransactionDB * db;
	::rocksdb::Transaction * txn{ nullptr };
	bool active{ false };
};
}
