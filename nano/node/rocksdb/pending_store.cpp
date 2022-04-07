#include <nano/node/lmdb/pending_store.hpp>

#include <nano/node/rocksdb/rocksdb.hpp>

nano::pending_store_rocksdb::pending_store_rocksdb (nano::rocksdb_store & store) :
	store{ store }
{
};

void nano::pending_store_rocksdb::put (nano::write_transaction const & transaction, nano::pending_key const & key, nano::pending_info const & pending)
{
	auto status = store.put (transaction, tables::pending, key, pending);
	release_assert_success (store, status);
}

void nano::pending_store_rocksdb::del (nano::write_transaction const & transaction, nano::pending_key const & key)
{
	auto status = store.del (transaction, tables::pending, key);
	release_assert_success (store, status);
}

bool nano::pending_store_rocksdb::get (nano::transaction const & transaction, nano::pending_key const & key, nano::pending_info & pending)
{
	nano::rocksdb_val value;
	auto status1 = store.get (transaction, tables::pending, key, value);
	release_assert (store.success (status1) || store.not_found (status1));
	bool result (true);
	if (store.success (status1))
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = pending.deserialize (stream);
	}
	return result;
}

bool nano::pending_store_rocksdb::exists (nano::transaction const & transaction_a, nano::pending_key const & key_a)
{
	auto iterator (begin (transaction_a, key_a));
	return iterator != end () && nano::pending_key (iterator->first) == key_a;
}

bool nano::pending_store_rocksdb::any (nano::transaction const & transaction_a, nano::account const & account_a)
{
	auto iterator (begin (transaction_a, nano::pending_key (account_a, 0)));
	return iterator != end () && nano::pending_key (iterator->first).account == account_a;
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::pending_store_rocksdb::begin (nano::transaction const & transaction_a, nano::pending_key const & key_a) const
{
	return static_cast<nano::store_partial<rocksdb::Slice, rocksdb_store> &> (store).template make_iterator<nano::pending_key, nano::pending_info> (transaction_a, tables::pending, key_a);
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::pending_store_rocksdb::begin (nano::transaction const & transaction_a) const
{
	return static_cast<nano::store_partial<rocksdb::Slice, rocksdb_store> &> (store).template make_iterator<nano::pending_key, nano::pending_info> (transaction_a, tables::pending);
}

nano::store_iterator<nano::pending_key, nano::pending_info> nano::pending_store_rocksdb::end () const
{
	return nano::store_iterator<nano::pending_key, nano::pending_info> (nullptr);
}

void nano::pending_store_rocksdb::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::pending_key, nano::pending_info>, nano::store_iterator<nano::pending_key, nano::pending_info>)> const & action_a) const
{
	parallel_traversal<nano::uint512_t> (
	[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
		nano::uint512_union union_start (start);
		nano::uint512_union union_end (end);
		nano::pending_key key_start (union_start.uint256s[0].number (), union_start.uint256s[1].number ());
		nano::pending_key key_end (union_end.uint256s[0].number (), union_end.uint256s[1].number ());
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, key_start), !is_last ? this->begin (transaction, key_end) : this->end ());
	});
}
