#include <nano/lib/assert.hpp>
#include <nano/store/rocksdb/transaction_rocksdb.hpp>

/*
 * read_transaction_impl
 */

nano::store::rocksdb::read_transaction_impl::read_transaction_impl (::rocksdb::DB * db_a) :
	db{ db_a }
{
	renew ();
}

nano::store::rocksdb::read_transaction_impl::~read_transaction_impl ()
{
	reset ();
}

void nano::store::rocksdb::read_transaction_impl::reset ()
{
	if (active)
	{
		db->ReleaseSnapshot (options.snapshot);
		active = false;
	}
}

void nano::store::rocksdb::read_transaction_impl::renew ()
{
	release_assert (db != nullptr);
	release_assert (!active);
	options.snapshot = db->GetSnapshot ();
	active = true;
}

void * nano::store::rocksdb::read_transaction_impl::get_handle () const
{
	return (void *)&options;
}

/*
 * write_transaction_impl
 */

nano::store::rocksdb::write_transaction_impl::write_transaction_impl (::rocksdb::TransactionDB * db_a) :
	db{ db_a }
{
	renew ();
}

nano::store::rocksdb::write_transaction_impl::~write_transaction_impl ()
{
	commit ();
}

void nano::store::rocksdb::write_transaction_impl::commit ()
{
	if (active)
	{
		auto status = txn->Commit ();
		release_assert (status.ok (), "Unable to write to the RocksDB database", status.ToString ());
		delete txn;
		txn = nullptr;
		active = false;
	}
}

void nano::store::rocksdb::write_transaction_impl::renew ()
{
	debug_assert (check_no_write_tx ());
	release_assert (!active);
	release_assert (txn == nullptr);
	::rocksdb::TransactionOptions txn_options;
	txn_options.set_snapshot = true;
	txn = db->BeginTransaction (::rocksdb::WriteOptions (), txn_options);
	active = true;
}

void * nano::store::rocksdb::write_transaction_impl::get_handle () const
{
	return txn;
}

bool nano::store::rocksdb::write_transaction_impl::contains (nano::store::table table) const
{
	return true;
}

bool nano::store::rocksdb::write_transaction_impl::check_no_write_tx () const
{
	std::vector<::rocksdb::Transaction *> transactions;
	db->GetAllPreparedTransactions (&transactions);
	return transactions.empty ();
}
