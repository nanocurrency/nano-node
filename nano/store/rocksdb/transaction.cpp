#include <nano/store/rocksdb/transaction_impl.hpp>

nano::store::rocksdb::read_transaction_impl::read_transaction_impl (::rocksdb::DB * db_a) :
	db (db_a)
{
	if (db_a)
	{
		options.snapshot = db_a->GetSnapshot ();
	}
}

nano::store::rocksdb::read_transaction_impl::~read_transaction_impl ()
{
	reset ();
}

void nano::store::rocksdb::read_transaction_impl::reset ()
{
	if (db)
	{
		db->ReleaseSnapshot (options.snapshot);
	}
}

void nano::store::rocksdb::read_transaction_impl::renew ()
{
	options.snapshot = db->GetSnapshot ();
}

void * nano::store::rocksdb::read_transaction_impl::get_handle () const
{
	return (void *)&options;
}

nano::store::rocksdb::write_transaction_impl::write_transaction_impl (::rocksdb::TransactionDB * db_a) :
	db (db_a)
{
	::rocksdb::TransactionOptions txn_options;
	txn_options.set_snapshot = true;
	txn = db->BeginTransaction (::rocksdb::WriteOptions (), txn_options);
}

nano::store::rocksdb::write_transaction_impl::~write_transaction_impl ()
{
	commit ();
	delete txn;
}

void nano::store::rocksdb::write_transaction_impl::commit ()
{
	if (active)
	{
		auto status = txn->Commit ();
		release_assert (status.ok () && "Unable to write to the RocksDB database", status.ToString ());
		active = false;
	}
}

void nano::store::rocksdb::write_transaction_impl::renew ()
{
	::rocksdb::TransactionOptions txn_options;
	txn_options.set_snapshot = true;
	db->BeginTransaction (::rocksdb::WriteOptions (), txn_options, txn);
	active = true;
}

void * nano::store::rocksdb::write_transaction_impl::get_handle () const
{
	return txn;
}

bool nano::store::rocksdb::write_transaction_impl::contains (nano::tables table_a) const
{
	return true;
}
