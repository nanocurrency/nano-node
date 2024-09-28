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
	std::pair<::rocksdb::DB *, ::rocksdb::ReadOptions> internals;
};

class write_transaction_impl final : public store::write_transaction_impl
{
public:
	write_transaction_impl (::rocksdb::TransactionDB * db_a);
	~write_transaction_impl ();
	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (nano::tables table_a) const override;

private:
	bool check_no_write_tx () const;

	std::pair<::rocksdb::TransactionDB *, ::rocksdb::Transaction *> internals;
	bool active{ true };
};
}
