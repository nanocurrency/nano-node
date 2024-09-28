#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/rocksdb/confirmation_height.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>
#include <nano/store/rocksdb/utility.hpp>

nano::store::rocksdb::confirmation_height::confirmation_height (nano::store::rocksdb::component & store) :
	store{ store }
{
}

void nano::store::rocksdb::confirmation_height::put (store::write_transaction const & transaction, nano::account const & account, nano::confirmation_height_info const & confirmation_height_info)
{
	auto status = rocksdb::put (transaction, store.table_to_column_family (tables::confirmation_height), account, confirmation_height_info);
	store.release_assert_success (status);
}

bool nano::store::rocksdb::confirmation_height::get (store::transaction const & transaction, nano::account const & account, nano::confirmation_height_info & confirmation_height_info)
{
	nano::store::rocksdb::db_val value;
	auto status = rocksdb::get (transaction, store.table_to_column_family (tables::confirmation_height), account, value);
	release_assert (store.success (status) || store.not_found (status));
	bool result (true);
	if (store.success (status))
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = confirmation_height_info.deserialize (stream);
	}
	if (result)
	{
		confirmation_height_info.height = 0;
		confirmation_height_info.frontier = 0;
	}

	return result;
}

bool nano::store::rocksdb::confirmation_height::exists (store::transaction const & transaction, nano::account const & account) const
{
	return rocksdb::exists (transaction, store.table_to_column_family (tables::confirmation_height), account);
}

void nano::store::rocksdb::confirmation_height::del (store::write_transaction const & transaction, nano::account const & account)
{
	auto status = rocksdb::del (transaction, store.table_to_column_family (tables::confirmation_height), account);
	store.release_assert_success (status);
}

uint64_t nano::store::rocksdb::confirmation_height::count (store::transaction const & transaction)
{
	return store.count (transaction, tables::confirmation_height);
}

void nano::store::rocksdb::confirmation_height::clear (store::write_transaction const & transaction, nano::account const & account)
{
	del (transaction, account);
}

void nano::store::rocksdb::confirmation_height::clear (store::write_transaction const & transaction)
{
	store.drop (transaction, nano::tables::confirmation_height);
}

nano::store::iterator<nano::account, nano::confirmation_height_info> nano::store::rocksdb::confirmation_height::begin (store::transaction const & transaction, nano::account const & account) const
{
	return store.make_iterator<nano::account, nano::confirmation_height_info> (transaction, tables::confirmation_height, account);
}

nano::store::iterator<nano::account, nano::confirmation_height_info> nano::store::rocksdb::confirmation_height::begin (store::transaction const & transaction) const
{
	return store.make_iterator<nano::account, nano::confirmation_height_info> (transaction, tables::confirmation_height);
}

nano::store::iterator<nano::account, nano::confirmation_height_info> nano::store::rocksdb::confirmation_height::end () const
{
	return store::iterator<nano::account, nano::confirmation_height_info> (nullptr);
}

void nano::store::rocksdb::confirmation_height::for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::account, nano::confirmation_height_info>, store::iterator<nano::account, nano::confirmation_height_info>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}
