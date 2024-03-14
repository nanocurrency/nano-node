#include <nano/lib/blocks.hpp>
#include <nano/secure/utility.hpp>
#include <nano/store/rocksdb/transaction_impl.hpp>
#include <nano/store/rocksdb/unconfirmed_set.hpp>
#include <nano/store/pending.hpp>

#include <filesystem>

nano::store::unconfirmed_set::unconfirmed_set () :
	env{ init () },
	account{ *env },
	block{ *env },
	receivable{ *env },
	received{ *env },
	rep_weight{ *env },
	successor{ *env }
{
}

nano::store::unconfirmed_write_transaction nano::store::unconfirmed_set::tx_begin_write () const
{
	release_assert (env != nullptr);
	return store::unconfirmed_write_transaction{ std::make_unique<nano::store::rocksdb::write_transaction_impl> (env.get()) };
}

nano::store::unconfirmed_read_transaction nano::store::unconfirmed_set::tx_begin_read () const
{
	release_assert (env != nullptr);
	return store::unconfirmed_read_transaction{ std::make_unique<nano::store::rocksdb::read_transaction_impl> (env.get ()) };
}

bool nano::store::unconfirmed_set::receivable_exists (store::unconfirmed_transaction const & tx, nano::account const & account) const
{
	nano::pending_key begin{ account, 0 };
	nano::pending_key end{ account.number () + 1, 0 };
	auto item = receivable.lower_bound (tx, account, 0);
	return item.has_value () && item.value ().first.account == account;
}

auto nano::store::unconfirmed_set::init () const -> ::rocksdb::TransactionDB *
{
	::rocksdb::Options options;
	options.create_if_missing = true;
	options.OptimizeLevelStyleCompaction ();
	options.compression = ::rocksdb::kNoCompression;

	auto path = std::filesystem::temp_directory_path () / nano::random_filename ();
	::rocksdb::TransactionDB * env;
	auto status = ::rocksdb::TransactionDB::Open (options, ::rocksdb::TransactionDBOptions{}, path.string ().c_str (), &env);
	release_assert (status.ok ());
	return env;
}
