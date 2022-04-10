#include <nano/node/rocksdb/confirmation_height_store.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>

nano::rocksdb::confirmation_height_store::confirmation_height_store (nano::rocksdb_store & store) :
	store{ store }
{
}

void nano::rocksdb::confirmation_height_store::put (nano::write_transaction const & transaction, nano::account const & account, nano::confirmation_height_info const & confirmation_height_info)
{
	auto status = store.put (transaction, tables::confirmation_height, account, confirmation_height_info);
	store.release_assert_success (status);
}

bool nano::rocksdb::confirmation_height_store::get (nano::transaction const & transaction, nano::account const & account, nano::confirmation_height_info & confirmation_height_info)
{
	nano::rocksdb_val value;
	auto status = store.get (transaction, tables::confirmation_height, account, value);
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

bool nano::rocksdb::confirmation_height_store::exists (nano::transaction const & transaction, nano::account const & account) const
{
	return store.exists (transaction, tables::confirmation_height, account);
}

void nano::rocksdb::confirmation_height_store::del (nano::write_transaction const & transaction, nano::account const & account)
{
	auto status = store.del (transaction, tables::confirmation_height, account);
	store.release_assert_success (status);
}

uint64_t nano::rocksdb::confirmation_height_store::count (nano::transaction const & transaction)
{
	return store.count (transaction, tables::confirmation_height);
}

void nano::rocksdb::confirmation_height_store::clear (nano::write_transaction const & transaction, nano::account const & account)
{
	del (transaction, account);
}

void nano::rocksdb::confirmation_height_store::clear (nano::write_transaction const & transaction)
{
	store.drop (transaction, nano::tables::confirmation_height);
}

nano::store_iterator<nano::account, nano::confirmation_height_info> nano::rocksdb::confirmation_height_store::begin (nano::transaction const & transaction, nano::account const & account) const
{
	return store.make_iterator<nano::account, nano::confirmation_height_info> (transaction, tables::confirmation_height, account);
}

nano::store_iterator<nano::account, nano::confirmation_height_info> nano::rocksdb::confirmation_height_store::begin (nano::transaction const & transaction) const
{
	return store.make_iterator<nano::account, nano::confirmation_height_info> (transaction, tables::confirmation_height);
}

nano::store_iterator<nano::account, nano::confirmation_height_info> nano::rocksdb::confirmation_height_store::end () const
{
	return nano::store_iterator<nano::account, nano::confirmation_height_info> (nullptr);
}

void nano::rocksdb::confirmation_height_store::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::account, nano::confirmation_height_info>, nano::store_iterator<nano::account, nano::confirmation_height_info>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}
