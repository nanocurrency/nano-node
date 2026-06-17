#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/saturate.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/crawler.hpp>
#include <nano/store/fwd.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/store/typed_iterator_templ.hpp>

#include <limits>
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

	template <typename Transaction>
	auto crawl (Transaction & txn, nano::topo_key const & start = {}) const -> nano::store::crawler<topology_view, Transaction>
	{
		return nano::store::crawler<topology_view, Transaction>{ *this, txn, start };
	}

private:
	nano::store::backend & backend;
};
}

/**
 * Specialization for the topology table whose key is the full topo_key (topo_height + hash).
 * Every key is its own group, so seek_key_type is the whole topo_key and ordering uses its operator<=>.
 */
template <>
struct nano::store::crawler_traits<nano::topo_key, std::nullptr_t>
{
	using seek_key_type = nano::topo_key;

	static nano::topo_key make_iterator_key (seek_key_type const & key)
	{
		return key;
	}

	static seek_key_type group_key (nano::topo_key const & key)
	{
		return key;
	}

	static nano::topo_key comparable (seek_key_type const & key)
	{
		return key;
	}

	static seek_key_type next_group (seek_key_type const & current)
	{
		// Smallest key strictly greater than current: bump the hash, carrying into topo_height on overflow
		if (current.hash.number () < std::numeric_limits<nano::uint256_t>::max ())
		{
			return nano::topo_key{ current.topo_height, nano::block_hash{ current.hash.number () + 1 } };
		}
		return nano::topo_key{ nano::inc_sat (current.topo_height), nano::block_hash{ 0 } };
	}
};
