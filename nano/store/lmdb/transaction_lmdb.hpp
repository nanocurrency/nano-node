#pragma once

#include <nano/lib/id_dispenser.hpp>
#include <nano/store/transaction.hpp>
#include <nano/store/txn_tracking.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
class env;

class read_transaction_impl final : public nano::store::read_transaction_impl
{
public:
	explicit read_transaction_impl (nano::store::lmdb::env const &, nano::store::txn_callbacks txn_callbacks = {});
	~read_transaction_impl () override;

	void reset () override;
	void renew () override;
	void * get_handle () const override;

private:
	nano::store::lmdb::env const & env;
	nano::store::txn_callbacks txn_callbacks;
	MDB_txn * handle{ nullptr };
	bool active{ false };
};

class write_transaction_impl final : public nano::store::write_transaction_impl
{
public:
	explicit write_transaction_impl (nano::store::lmdb::env const &, nano::store::txn_callbacks txn_callbacks = {});
	~write_transaction_impl () override;

	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (nano::store::table) const override;

private:
	nano::store::lmdb::env const & env;
	nano::store::txn_callbacks txn_callbacks;
	MDB_txn * handle{ nullptr };
	bool active{ false };
};
}
