#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/rocksdb/unconfirmed_account.hpp>
#include <nano/store/rocksdb/unconfirmed_block.hpp>
#include <nano/store/rocksdb/unconfirmed_pending.hpp>
#include <nano/store/rocksdb/unconfirmed_received.hpp>
#include <nano/store/rocksdb/unconfirmed_rep_weight.hpp>
#include <nano/store/rocksdb/unconfirmed_successor.hpp>
#include <nano/store/rocksdb/unconfirmed_transaction.hpp>

#include <rocksdb/utilities/transaction_db.h>

#include <memory>

namespace nano::store
{
class unconfirmed_set
{
public:
	unconfirmed_set ();

	bool receivable_exists (store::unconfirmed_transaction const & tx, nano::account const & account) const;
	store::unconfirmed_write_transaction tx_begin_write () const;
	store::unconfirmed_read_transaction tx_begin_read () const;

private:
	::rocksdb::TransactionDB * init () const;
	std::unique_ptr<::rocksdb::TransactionDB> env;

public:
	store::rocksdb::unconfirmed_account account;
	store::rocksdb::unconfirmed_block block;
	store::rocksdb::unconfirmed_pending receivable;
	store::rocksdb::unconfirmed_received received;
	store::rocksdb::unconfirmed_rep_weight rep_weight;
	store::rocksdb::unconfirmed_successor successor;
};
}
