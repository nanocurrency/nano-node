#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <cstddef>

namespace mi = boost::multi_index;

namespace nano::bootstrap
{
/*
 * Topo gaps engine: tracks blocks the topo strategy submitted that came back from the block processor with
 * a gap status (a dependency is still missing). The live count is the spearhead back-pressure signal: while
 * many of our submissions are stuck, forward discovery pauses so repair heads can refill the missing deps.
 *
 * Entries leave the set when the block finally progresses, when its account is rolled back (its ancestors
 * changed under us), or after a TTL, so a permanently-missing dependency cannot wedge the spearhead forever.
 * Pure state, guarded externally by the bootstrap_context mutex.
 */
class topo_gaps
{
public:
	topo_gaps (topo_scan_config const &, nano::stats &);

	// Record a submitted block stuck on a missing dependency (insert-if-absent, refreshes the timestamp)
	void track (nano::block_hash const & hash, nano::account const & account, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());

	// Drop a gap once its block has been applied
	void resolve (nano::block_hash const & hash);

	// Drop every gap on an account whose chain was rolled back
	void rollback (nano::account const & account);

	// Drop gaps older than the configured TTL so a permanently-missing dependency cannot wedge the spearhead
	void evict (std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());

	// Number of stuck gaps (the spearhead back-pressure signal)
	std::size_t count () const;

	void reset ();

	nano::container_info container_info () const;

private: // Dependencies
	topo_scan_config const & config;
	nano::stats & stats;

private:
	struct entry
	{
		nano::block_hash hash;
		nano::account account; // The account whose chain this gap belongs to (zero if not derivable)
		std::chrono::steady_clock::time_point timestamp; // When the gap was last (re)tracked, for TTL eviction
	};

	// clang-format off
	class tag_hash {};
	class tag_account {};
	class tag_timestamp {};

	using ordered_gaps = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, nano::block_hash, &entry::hash>>,
		mi::hashed_non_unique<mi::tag<tag_account>,
			mi::member<entry, nano::account, &entry::account>>,
		mi::ordered_non_unique<mi::tag<tag_timestamp>,
			mi::member<entry, std::chrono::steady_clock::time_point, &entry::timestamp>>
	>>;
	// clang-format on

	ordered_gaps gaps;
};
}
