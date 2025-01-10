#pragma once

#include <celerix/lib/interval.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace mi = boost::multi_index;

namespace celerix
{
class vote_cache_config final
{
public:
	celerix::error deserialize (celerix::tomlconfig & toml);
	celerix::error serialize (celerix::tomlconfig & toml) const;

public:
	std::size_t max_size{ 1024 * 64 };
	std::size_t max_voters{ 64 };
	std::chrono::seconds age_cutoff{ 15 * 60 };
};

/**
 * Stores votes associated with a single block hash
 */
class vote_cache_entry final
{
private:
	struct voter_entry
	{
		celerix::account representative;
		celerix::uint128_t weight;
		std::shared_ptr<celerix::vote> vote;
	};

public:
	explicit vote_cache_entry (celerix::block_hash const & hash);

	/**
	 * Adds a vote into a list, checks for duplicates and updates timestamp if new one is greater
	 * @return true if current tally changed, false otherwise
	 */
	bool vote (std::shared_ptr<celerix::vote> const & vote, celerix::uint128_t const & rep_weight, std::size_t max_voters);

	std::size_t size () const;
	std::vector<std::shared_ptr<celerix::vote>> votes () const;

public: // Keep accessors inlined
	celerix::block_hash hash () const
	{
		return hash_m;
	}
	std::chrono::steady_clock::time_point last_vote () const
	{
		return last_vote_m;
	}
	celerix::uint128_t tally () const
	{
		return tally_m;
	}
	celerix::uint128_t final_tally () const
	{
		return final_tally_m;
	}

private:
	bool vote_impl (std::shared_ptr<celerix::vote> const & vote, celerix::uint128_t const & rep_weight, std::size_t max_voters);
	std::pair<celerix::uint128_t, celerix::uint128_t> calculate_tally () const; // <tally, final_tally>

	// clang-format off
	class tag_representative {};
	class tag_weight {};
	// clang-format on

	// clang-format off
	using ordered_voters = boost::multi_index_container<voter_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_representative>,
			mi::member<voter_entry, celerix::account, &voter_entry::representative>>,
		mi::ordered_non_unique<mi::tag<tag_weight>,
			mi::member<voter_entry, celerix::uint128_t, &voter_entry::weight>>
	>>;
	// clang-format on
	ordered_voters voters;

	celerix::block_hash const hash_m;
	std::chrono::steady_clock::time_point last_vote_m{};
	celerix::uint128_t tally_m{ 0 };
	celerix::uint128_t final_tally_m{ 0 };
};

class vote_cache final
{
public:
	using entry = vote_cache_entry;

public:
	explicit vote_cache (vote_cache_config const &, celerix::stats &);

	/**
	 * Adds a new vote to cache
	 */
	void insert (
	std::shared_ptr<celerix::vote> const & vote,
	std::unordered_map<celerix::block_hash, celerix::vote_code> const & results = {});

	/**
	 * Tries to find an entry associated with block hash
	 */
	std::vector<std::shared_ptr<celerix::vote>> find (celerix::block_hash const & hash) const;
	bool contains (celerix::block_hash const & hash) const;

	/**
	 * Removes an entry associated with block hash, does nothing if entry does not exist
	 * @return true if hash existed and was erased, false otherwise
	 */
	bool erase (celerix::block_hash const & hash);
	void clear ();

	std::size_t size () const;
	bool empty () const;

	struct top_entry
	{
		celerix::block_hash hash;
		celerix::uint128_t tally;
		celerix::uint128_t final_tally;
	};

	/**
	 * Returns blocks with highest observed tally
	 * The blocks are sorted in descending order by final tally, then by tally
	 * @param min_tally minimum tally threshold, entries below with their voting weight below this will be ignored
	 */
	std::deque<top_entry> top (celerix::uint128_t const & min_tally);

	celerix::container_info container_info () const;

public:
	/**
	 * Function used to query rep weight for tally calculation
	 */
	std::function<celerix::uint128_t (celerix::account const &)> rep_weight_query{ [] (celerix::account const & rep) { debug_assert (false); return 0; } };

private: // Dependencies
	vote_cache_config const & config;
	celerix::stats & stats;

private:
	void insert_impl (std::shared_ptr<celerix::vote> const &, celerix::block_hash const & hash, celerix::uint128_t const & rep_weight);
	void cleanup ();

	// clang-format off
	class tag_sequenced {};
	class tag_hash {};
	class tag_tally {};
	// clang-format on

	// clang-format off
	using ordered_cache = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<entry, celerix::block_hash, &entry::hash>>,
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::ordered_non_unique<mi::tag<tag_tally>,
			mi::const_mem_fun<entry, celerix::uint128_t, &entry::tally>, std::greater<>> // DESC
	>>;
	// clang-format on
	ordered_cache cache;

	mutable celerix::mutex mutex;
	celerix::interval cleanup_interval;
};
}
