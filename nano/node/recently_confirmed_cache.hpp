#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

namespace mi = boost::multi_index;

namespace nano
{
class container_info_component;
}

namespace nano
{
class recently_confirmed_cache final
{
public:
	using entry_t = std::pair<nano::qualified_root, nano::block_hash>;

	explicit recently_confirmed_cache (std::size_t max_size);

	void put (nano::qualified_root const &, nano::block_hash const &);
	void erase (nano::block_hash const &);
	void clear ();
	std::size_t size () const;

	bool exists (nano::qualified_root const &) const;
	bool exists (nano::block_hash const &) const;

	nano::container_info container_info () const;

public: // Tests
	entry_t back () const;

private:
	// clang-format off
	class tag_hash {};
	class tag_root {};
	class tag_sequence {};

	using ordered_recent_confirmations = boost::multi_index_container<entry_t,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequence>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry_t, nano::qualified_root, &entry_t::first>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry_t, nano::block_hash, &entry_t::second>>>>;
	// clang-format on
	ordered_recent_confirmations confirmed;

	std::size_t const max_size;

	mutable nano::mutex mutex;
};
}
