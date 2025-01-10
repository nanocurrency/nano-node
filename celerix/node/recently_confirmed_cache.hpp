#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

namespace mi = boost::multi_index;

namespace celerix
{
class container_info_component;
}

namespace celerix
{
class recently_confirmed_cache final
{
public:
	using entry_t = std::pair<celerix::qualified_root, celerix::block_hash>;

	explicit recently_confirmed_cache (std::size_t max_size);

	void put (celerix::qualified_root const &, celerix::block_hash const &);
	void erase (celerix::block_hash const &);
	void clear ();
	std::size_t size () const;

	bool exists (celerix::qualified_root const &) const;
	bool exists (celerix::block_hash const &) const;

	celerix::container_info container_info () const;

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
			mi::member<entry_t, celerix::qualified_root, &entry_t::first>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry_t, celerix::block_hash, &entry_t::second>>>>;
	// clang-format on
	ordered_recent_confirmations confirmed;

	std::size_t const max_size;

	mutable celerix::mutex mutex;
};
}
