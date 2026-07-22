#include <nano/store/backend.hpp>
#include <nano/store/ledger/topology.hpp>

namespace nano::store::ledger
{
topology_view::topology_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void topology_view::put (nano::store::write_transaction const & txn, nano::topo_key const & key)
{
	auto status = backend.put (txn, nano::store::table::topology, key, nullptr);
	backend.release_assert_success (status);
}

void topology_view::del (nano::store::write_transaction const & txn, nano::topo_key const & key)
{
	auto status = backend.del (txn, nano::store::table::topology, key);
	backend.release_assert_success (status);
}

bool topology_view::exists (nano::store::transaction const & txn, nano::topo_key const & key) const
{
	return backend.exists (txn, nano::store::table::topology, key);
}

std::optional<nano::topo_key> topology_view::latest (nano::store::transaction const & txn) const
{
	auto first = begin (txn);
	auto i = end (txn);
	if (i == first)
	{
		return std::nullopt;
	}
	--i;
	return i->first;
}

uint64_t topology_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::topology);
}

void topology_view::clear ()
{
	auto status = backend.clear (nano::store::table::topology);
	backend.release_assert_success (status);
}

auto topology_view::begin (nano::store::transaction const & txn, nano::topo_key const & key) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::topology, key) };
}

auto topology_view::begin (nano::store::transaction const & txn, uint64_t topo_height) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::topology, nano::topo_key{ topo_height, 0 }) };
}

auto topology_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::topology) };
}

auto topology_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::topology) };
}
}
