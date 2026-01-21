#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/ledger/pending.hpp>

namespace nano::store::ledger
{
pending_view::pending_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void pending_view::put (nano::store::write_transaction const & txn, nano::pending_key const & key, nano::pending_info const & pending)
{
	auto status = backend.put (txn, nano::store::table::pending, key, pending);
	backend.release_assert_success (status);
}

void pending_view::del (nano::store::write_transaction const & txn, nano::pending_key const & key)
{
	auto status = backend.del (txn, nano::store::table::pending, key);
	backend.release_assert_success (status);
}

auto pending_view::get (nano::store::transaction const & txn, nano::pending_key const & key) const -> std::optional<nano::pending_info>
{
	nano::store::db_val value;
	auto status = backend.get (txn, nano::store::table::pending, key, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	std::optional<nano::pending_info> result;
	if (backend.success (status))
	{
		// TODO: Use db_val serialization operators
		nano::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()), value.size () };
		result = nano::pending_info{};
		auto error = result.value ().deserialize (stream);
		release_assert (!error);
	}
	return result;
}

bool pending_view::exists (nano::store::transaction const & txn, nano::pending_key const & key) const
{
	return backend.exists (txn, nano::store::table::pending, key);
}

bool pending_view::any (nano::store::transaction const & txn, nano::account const & account) const
{
	auto iterator = begin (txn, nano::pending_key{ account, 0 });
	return iterator != end (txn) && nano::pending_key (iterator->first).account == account;
}

auto pending_view::begin (nano::store::transaction const & txn, nano::pending_key const & key) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::pending, key) };
}

auto pending_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::pending) };
}

auto pending_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::pending) };
}

auto pending_view::crawl (nano::store::transaction const & txn, nano::account const & start) const -> crawler
{
	return crawler{ *this, txn, start };
}

void pending_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<nano::uint512_t> (
	[&action, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
		nano::uint512_union union_start{ start };
		nano::uint512_union union_end{ end };
		nano::pending_key key_start{ union_start.uint256s[0].number (), union_start.uint256s[1].number () };
		nano::pending_key key_end{ union_end.uint256s[0].number (), union_end.uint256s[1].number () };
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, key_start), !is_last ? this->begin (txn, key_end) : this->end (txn));
	});
}
}
