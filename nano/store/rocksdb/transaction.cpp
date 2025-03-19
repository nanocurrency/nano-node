#include <nano/lib/logging.hpp>
#include <nano/store/rocksdb/transaction_impl.hpp>

nano::store::rocksdb::read_transaction_impl::read_transaction_impl (nano::logger & logger, ::rocksdb::DB * db_a) :
	store::read_transaction_impl{ logger },
	db{ db_a }
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

nano::store::rocksdb::write_transaction_impl::write_transaction_impl (nano::logger & logger, ::rocksdb::TransactionDB * db_a) :
	store::write_transaction_impl{ logger },
	db{ db_a }
{
	debug_assert (check_no_write_tx ());
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
		if (!status.ok ())
		{
			logger.critical (nano::log::type::rocksdb, "Unable to commit write transaction {}", status.ToString ());
			release_assert (false);
		}
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

bool nano::store::rocksdb::write_transaction_impl::check_no_write_tx () const
{
	std::vector<::rocksdb::Transaction *> transactions;
	db->GetAllPreparedTransactions (&transactions);
	return transactions.empty ();
}
