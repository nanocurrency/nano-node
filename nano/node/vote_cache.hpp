#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
class node;
class active_transactions;
class election;
class vote;
}

namespace nano
{
class vote_cache_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

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
		nano::account representative;
		nano::uint128_t weight;
		std::shared_ptr<nano::vote> vote;
	};

public:
	explicit vote_cache_entry (nano::block_hash const & hash);

	/**
	 * Adds a vote into a list, checks for duplicates and updates timestamp if new one is greater
	 * @return true if current tally changed, false otherwise
	 */
	bool vote (std::shared_ptr<nano::vote> const & vote, nano::uint128_t const & rep_weight, std::size_t max_voters);

	std::size_t size () const;
	nano::block_hash hash () const;
	nano::uint128_t tally () const;
	nano::uint128_t final_tally () const;
	std::vector<std::shared_ptr<nano::vote>> votes () const;
	std::chrono::steady_clock::time_point last_vote () const;

private:
	bool vote_impl (std::shared_ptr<nano::vote> const & vote, nano::uint128_t const & rep_weight, std::size_t max_voters);

	// clang-format off
	class tag_representative {};
	class tag_weight {};
	// clang-format on

	// clang-format off
	using ordered_voters = boost::multi_index_container<voter_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_representative>,
			mi::member<voter_entry, nano::account, &voter_entry::representative>>,
		mi::ordered_non_unique<mi::tag<tag_weight>,
			mi::member<voter_entry, nano::uint128_t, &voter_entry::weight>>
	>>;
	// clang-format on
	ordered_voters voters;

	nano::block_hash const hash_m;
	std::chrono::steady_clock::time_point last_vote_m{};
};

class vote_cache final
{
public:
	using entry = vote_cache_entry;

public:
	explicit vote_cache (vote_cache_config const &, nano::stats &);

	/**
	 * Adds a new vote to cache
	 */
	void insert (
	std::shared_ptr<nano::vote> const & vote,
	std::function<bool (nano::block_hash const &)> filter = [] (nano::block_hash const &) { return true; });

	/**
	 * Should be called for every processed vote, filters which votes should be added to cache
	 */
	void observe (std::shared_ptr<nano::vote> const & vote, nano::vote_source source, std::unordered_map<nano::block_hash, nano::vote_code>);

	/**
	 * Tries to find an entry associated with block hash
	 */
	std::vector<std::shared_ptr<nano::vote>> find (nano::block_hash const & hash) const;

	/**
	 * Removes an entry associated with block hash, does nothing if entry does not exist
	 * @return true if hash existed and was erased, false otherwise
	 */
	bool erase (nano::block_hash const & hash);
	void clear ();

	std::size_t size () const;
	bool empty () const;

public:
	struct top_entry
	{
		nano::block_hash hash;
		nano::uint128_t tally;
		nano::uint128_t final_tally;
	};

	/**
	 * Returns blocks with highest observed tally
	 * The blocks are sorted in descending order by final tally, then by tally
	 * @param min_tally minimum tally threshold, entries below with their voting weight below this will be ignored
	 */
	std::vector<top_entry> top (nano::uint128_t const & min_tally);

public: // Container info
	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name) const;

public:
	/**
	 * Function used to query rep weight for tally calculation
	 */
	std::function<nano::uint128_t (nano::account const &)> rep_weight_query{ [] (nano::account const & rep) { debug_assert (false); return 0; } };

private: // Dependencies
	vote_cache_config const & config;
	nano::stats & stats;

private:
	void cleanup ();

	// clang-format off
	class tag_sequenced {};
	class tag_hash {};
	// clang-format on

	// clang-format off
	using ordered_cache = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<entry, nano::block_hash, &entry::hash>>,
		mi::sequenced<mi::tag<tag_sequenced>>
	>>;
	// clang-format on
	ordered_cache cache;

	mutable nano::mutex mutex;
	nano::interval cleanup_interval;
};
}