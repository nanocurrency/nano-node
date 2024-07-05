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

nano::store::rocksdb::write_transaction_impl::write_transaction_impl (::rocksdb::TransactionDB * db_a, std::vector<nano::tables> const & tables_requiring_locks_a, std::vector<nano::tables> const & tables_no_locks_a, std::unordered_map<nano::tables, nano::mutex> & mutexes_a) :
	db (db_a),
	tables_requiring_locks (tables_requiring_locks_a),
	tables_no_locks (tables_no_locks_a),
	mutexes (mutexes_a)
{
	lock ();
	::rocksdb::TransactionOptions txn_options;
	txn_options.set_snapshot = true;
	txn = db->BeginTransaction (::rocksdb::WriteOptions (), txn_options);
}

nano::store::rocksdb::write_transaction_impl::~write_transaction_impl ()
{
	commit ();
	delete txn;
	unlock ();
}

void nano::store::rocksdb::write_transaction_impl::commit ()
{
	if (active)
	{
		auto status = txn->Commit ();

		// If there are no available memtables try again a few more times
		constexpr auto num_attempts = 10;
		auto attempt_num = 0;
		while (status.IsTryAgain () && attempt_num < num_attempts)
		{
			status = txn->Commit ();
			++attempt_num;
		}

		if (!status.ok ())
		{
			release_assert (false && "Unable to write to the RocksDB database", status.ToString ());
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

void nano::store::rocksdb::write_transaction_impl::lock ()
{
	for (auto table : tables_requiring_locks)
	{
		mutexes.at (table).lock ();
	}
}

void nano::store::rocksdb::write_transaction_impl::unlock ()
{
	for (auto table : tables_requiring_locks)
	{
		mutexes.at (table).unlock ();
	}
}

bool nano::store::rocksdb::write_transaction_impl::contains (nano::tables table_a) const
{
	return (std::find (tables_requiring_locks.begin (), tables_requiring_locks.end (), table_a) != tables_requiring_locks.end ()) || (std::find (tables_no_locks.begin (), tables_no_locks.end (), table_a) != tables_no_locks.end ());
}
