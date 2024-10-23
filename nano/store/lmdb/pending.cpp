#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/lmdb/pending.hpp>

nano::store::lmdb::pending::pending (nano::store::lmdb::component & store) :
	store{ store } {};

void nano::store::lmdb::pending::put (store::write_transaction const & transaction, nano::pending_key const & key, nano::pending_info const & pending)
{
	auto status = store.put (transaction, tables::pending, key, pending);
	store.release_assert_success (status);
}

void nano::store::lmdb::pending::del (store::write_transaction const & transaction, nano::pending_key const & key)
{
	auto status = store.del (transaction, tables::pending, key);
	store.release_assert_success (status);
}

std::optional<nano::pending_info> nano::store::lmdb::pending::get (store::transaction const & transaction, nano::pending_key const & key)
{
	nano::store::lmdb::db_val value;
	auto status1 = store.get (transaction, tables::pending, key, value);
	release_assert (store.success (status1) || store.not_found (status1));
	std::optional<nano::pending_info> result;
	if (store.success (status1))
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = nano::pending_info{};
		auto error = result.value ().deserialize (stream);
		release_assert (!error);
	}
	return result;
}

bool nano::store::lmdb::pending::exists (store::transaction const & transaction_a, nano::pending_key const & key_a)
{
	auto iterator (begin (transaction_a, key_a));
	return iterator != end (transaction_a) && nano::pending_key (iterator->first) == key_a;
}

bool nano::store::lmdb::pending::any (store::transaction const & transaction_a, nano::account const & account_a)
{
	auto iterator (begin (transaction_a, nano::pending_key (account_a, 0)));
	return iterator != end (transaction_a) && nano::pending_key (iterator->first).account == account_a;
}

auto nano::store::lmdb::pending::begin (store::transaction const & transaction_a, nano::pending_key const & key_a) const -> iterator
{
	lmdb::db_val val{ key_a };
	return iterator{ store::iterator{ lmdb::iterator::lower_bound (store.env.tx (transaction_a), pending_handle, val) } };
}

auto nano::store::lmdb::pending::begin (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction_a), pending_handle) } };
}

auto nano::store::lmdb::pending::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (transaction_a), pending_handle) } };
}

void nano::store::lmdb::pending::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<nano::uint512_t> (
	[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
		nano::uint512_union union_start (start);
		nano::uint512_union union_end (end);
		nano::pending_key key_start (union_start.uint256s[0].number (), union_start.uint256s[1].number ());
		nano::pending_key key_end (union_end.uint256s[0].number (), union_end.uint256s[1].number ());
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, key_start), !is_last ? this->begin (transaction, key_end) : this->end (transaction));
	});
}
