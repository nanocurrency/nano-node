#include <nano/store/ledger/peer.hpp>

namespace nano::store::ledger
{
peer_view::peer_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void peer_view::put (nano::store::write_transaction const & txn, nano::endpoint_key const & endpoint, nano::millis_t timestamp)
{
	auto status = backend.put (txn, nano::store::table::peers, endpoint, timestamp);
	backend.release_assert_success (status);
}

nano::millis_t peer_view::get (nano::store::transaction const & txn, nano::endpoint_key const & endpoint) const
{
	nano::millis_t result{ 0 };
	nano::store::db_val value;
	auto status = backend.get (txn, nano::store::table::peers, endpoint, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	if (backend.success (status) && value.size () > 0)
	{
		result = static_cast<nano::millis_t> (value);
	}
	return result;
}

void peer_view::del (nano::store::write_transaction const & txn, nano::endpoint_key const & endpoint)
{
	auto status = backend.del (txn, nano::store::table::peers, endpoint);
	backend.release_assert_success (status);
}

bool peer_view::exists (nano::store::transaction const & txn, nano::endpoint_key const & endpoint) const
{
	return backend.exists (txn, nano::store::table::peers, endpoint);
}

size_t peer_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::peers);
}

void peer_view::clear ()
{
	auto status = backend.clear (nano::store::table::peers);
	backend.release_assert_success (status);
}

auto peer_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::peers) };
}

auto peer_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::peers) };
}
}
