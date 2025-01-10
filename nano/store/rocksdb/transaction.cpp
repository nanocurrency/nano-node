#include <celerix/store/rocksdb/transaction_impl.hpp>

celerix::store::rocksdb::read_transaction_impl::read_transaction_impl (::rocksdb::DB * db_a) :
	db (db_a)
{
	if (db_a)
	{
		options.snapshot = db_a->GetSnapshot ();
	}
}

celerix::store::rocksdb::read_transaction_impl::~read_transaction_impl ()
{
	reset ();
}

void celerix::store::rocksdb::read_transaction_impl::reset ()
{
	if (db)
	{
		db->ReleaseSnapshot (options.snapshot);
	}
}

void celerix::store::rocksdb::read_transaction_impl::renew ()
{
	options.snapshot = db->GetSnapshot ();
}

void * celerix::store::rocksdb::read_transaction_impl::get_handle () const
{
	return (void *)&options;
}

celerix::store::rocksdb::write_transaction_impl::write_transaction_impl (::rocksdb::TransactionDB * db_a) :
	db (db_a)
{
	debug_assert (check_no_write_tx ());
	::rocksdb::TransactionOptions txn_options;
	txn_options.set_snapshot = true;
	txn = db->BeginTransaction (::rocksdb::WriteOptions (), txn_options);
}

celerix::store::rocksdb::write_transaction_impl::~write_transaction_impl ()
{
	commit ();
	delete txn;
}

void celerix::store::rocksdb::write_transaction_impl::commit ()
{
	if (active)
	{
		auto status = txn->Commit ();
		release_assert (status.ok () && "Unable to write to the RocksDB database", status.ToString ());
		active = false;
	}
}

void celerix::store::rocksdb::write_transaction_impl::renew ()
{
	::rocksdb::TransactionOptions txn_options;
	txn_options.set_snapshot = true;
	db->BeginTransaction (::rocksdb::WriteOptions (), txn_options, txn);
	active = true;
}

void * celerix::store::rocksdb::write_transaction_impl::get_handle () const
{
	return txn;
}

bool celerix::store::rocksdb::write_transaction_impl::contains (celerix::tables table_a) const
{
	return true;
}

bool celerix::store::rocksdb::write_transaction_impl::check_no_write_tx () const
{
	std::vector<::rocksdb::Transaction *> transactions;
	db->GetAllPreparedTransactions (&transactions);
	return transactions.empty ();
}
