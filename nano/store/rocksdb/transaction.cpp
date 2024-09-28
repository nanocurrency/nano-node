#include <nano/store/rocksdb/transaction_impl.hpp>

nano::store::rocksdb::read_transaction_impl::read_transaction_impl (::rocksdb::DB * db_a)
{
	internals.first = db_a;
	if (db_a)
	{
		internals.second.snapshot = db_a->GetSnapshot ();
	}
}

nano::store::rocksdb::read_transaction_impl::~read_transaction_impl ()
{
	reset ();
}

void nano::store::rocksdb::read_transaction_impl::reset ()
{
	if (internals.first)
	{
		internals.first->ReleaseSnapshot (internals.second.snapshot);
	}
}

void nano::store::rocksdb::read_transaction_impl::renew ()
{
	internals.second.snapshot = internals.first->GetSnapshot ();
}

void * nano::store::rocksdb::read_transaction_impl::get_handle () const
{
	return (void *)&internals;
}

nano::store::rocksdb::write_transaction_impl::write_transaction_impl (::rocksdb::TransactionDB * db_a)
{
	internals.first = db_a;
	debug_assert (check_no_write_tx ());
	::rocksdb::TransactionOptions txn_options;
	txn_options.set_snapshot = true;
	internals.second = db_a->BeginTransaction (::rocksdb::WriteOptions (), txn_options);
}

nano::store::rocksdb::write_transaction_impl::~write_transaction_impl ()
{
	commit ();
	delete internals.second;
}

void nano::store::rocksdb::write_transaction_impl::commit ()
{
	if (active)
	{
		auto status = internals.second->Commit ();
		release_assert (status.ok () && "Unable to write to the RocksDB database", status.ToString ());
		active = false;
	}
}

void nano::store::rocksdb::write_transaction_impl::renew ()
{
	::rocksdb::TransactionOptions txn_options;
	txn_options.set_snapshot = true;
	internals.first->BeginTransaction (::rocksdb::WriteOptions (), txn_options, internals.second);
	active = true;
}

void * nano::store::rocksdb::write_transaction_impl::get_handle () const
{
	return (void *)&internals;
}

bool nano::store::rocksdb::write_transaction_impl::contains (nano::tables table_a) const
{
	return true;
}

bool nano::store::rocksdb::write_transaction_impl::check_no_write_tx () const
{
	std::vector<::rocksdb::Transaction *> transactions;
	internals.first->GetAllPreparedTransactions (&transactions);
	return transactions.empty ();
}
