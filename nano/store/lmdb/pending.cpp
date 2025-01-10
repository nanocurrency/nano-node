#include <celerix/secure/parallel_traversal.hpp>
#include <celerix/store/lmdb/lmdb.hpp>
#include <celerix/store/lmdb/pending.hpp>

celerix::store::lmdb::pending::pending (celerix::store::lmdb::component & store) :
	store{ store } {};

void celerix::store::lmdb::pending::put (store::write_transaction const & transaction, celerix::pending_key const & key, celerix::pending_info const & pending)
{
	auto status = store.put (transaction, tables::pending, key, pending);
	store.release_assert_success (status);
}

void celerix::store::lmdb::pending::del (store::write_transaction const & transaction, celerix::pending_key const & key)
{
	auto status = store.del (transaction, tables::pending, key);
	store.release_assert_success (status);
}

std::optional<celerix::pending_info> celerix::store::lmdb::pending::get (store::transaction const & transaction, celerix::pending_key const & key)
{
	celerix::store::lmdb::db_val value;
	auto status1 = store.get (transaction, tables::pending, key, value);
	release_assert (store.success (status1) || store.not_found (status1));
	std::optional<celerix::pending_info> result;
	if (store.success (status1))
	{
		celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = celerix::pending_info{};
		auto error = result.value ().deserialize (stream);
		release_assert (!error);
	}
	return result;
}

bool celerix::store::lmdb::pending::exists (store::transaction const & transaction_a, celerix::pending_key const & key_a)
{
	auto iterator (begin (transaction_a, key_a));
	return iterator != end (transaction_a) && celerix::pending_key (iterator->first) == key_a;
}

bool celerix::store::lmdb::pending::any (store::transaction const & transaction_a, celerix::account const & account_a)
{
	auto iterator (begin (transaction_a, celerix::pending_key (account_a, 0)));
	return iterator != end (transaction_a) && celerix::pending_key (iterator->first).account == account_a;
}

auto celerix::store::lmdb::pending::begin (store::transaction const & transaction_a, celerix::pending_key const & key_a) const -> iterator
{
	lmdb::db_val val{ key_a };
	return iterator{ store::iterator{ lmdb::iterator::lower_bound (store.env.tx (transaction_a), pending_handle, val) } };
}

auto celerix::store::lmdb::pending::begin (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction_a), pending_handle) } };
}

auto celerix::store::lmdb::pending::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (transaction_a), pending_handle) } };
}

void celerix::store::lmdb::pending::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<celerix::uint512_t> (
	[&action_a, this] (celerix::uint512_t const & start, celerix::uint512_t const & end, bool const is_last) {
		celerix::uint512_union union_start (start);
		celerix::uint512_union union_end (end);
		celerix::pending_key key_start (union_start.uint256s[0].number (), union_start.uint256s[1].number ());
		celerix::pending_key key_end (union_end.uint256s[0].number (), union_end.uint256s[1].number ());
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, key_start), !is_last ? this->begin (transaction, key_end) : this->end (transaction));
	});
}
