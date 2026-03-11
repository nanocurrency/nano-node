#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/election_status.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

namespace mi = boost::multi_index;

namespace nano
{
class recently_confirmed_cache final
{
public:
	explicit recently_confirmed_cache (std::size_t max_size);

	void put (nano::qualified_root const &, nano::block_hash const &, nano::election_status const &);
	void erase (nano::block_hash const &);
	void clear ();
	std::size_t size () const;

	bool contains (nano::qualified_root const &) const;
	bool contains (nano::block_hash const &) const;

	struct latency_stats
	{
		uint32_t p50{ 0 };
		uint32_t p90{ 0 };
		uint32_t p99{ 0 };
	};

	latency_stats latency_percentiles () const;

	nano::container_info container_info () const;

private:
	struct entry
	{
		nano::qualified_root root;
		nano::block_hash hash;
		nano::election_status status;
	};

	// clang-format off
	class tag_hash {};
	class tag_root {};
	class tag_sequenced {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry, nano::qualified_root, &entry::root>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, nano::block_hash, &entry::hash>>>>;
	// clang-format on
	ordered_entries entries;

	std::size_t const max_size;

	mutable nano::mutex mutex;
};
}
