#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/fwd.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/store/typed_iterator_templ.hpp>

#include <optional>

namespace nano::store::ledger
{
class topology_view
{
public:
	using iterator = nano::store::typed_iterator<nano::topo_key, std::nullptr_t>;

	explicit topology_view (nano::store::backend &);

	void put (nano::store::write_transaction const &, nano::topo_key const & key);
	void del (nano::store::write_transaction const &, nano::topo_key const & key);
	bool exists (nano::store::transaction const &, nano::topo_key const & key) const;
	std::optional<nano::topo_key> latest (nano::store::transaction const &) const;
	uint64_t count (nano::store::transaction const &) const;
	void clear ();

	iterator begin (nano::store::transaction const &, nano::topo_key const &) const;
	iterator begin (nano::store::transaction const &, uint64_t topo_height) const;
	iterator begin (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;

private:
	nano::store::backend & backend;
};
}
