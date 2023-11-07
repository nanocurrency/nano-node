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
	std::size_t max_size{ 1024 * 128 };
	std::size_t max_voters{ 128 };
	std::chrono::seconds age_cutoff{ 5 * 60 };
};

class vote_cache final
{
public:
	/**
	 * Stores votes associated with a single block hash
	 */
	class entry final
	{
	public:
		struct voter_entry
		{
			nano::account representative;
			uint64_t timestamp;
		};

	public:
		explicit entry (nano::block_hash const & hash);

		/**
		 * Adds a vote into a list, checks for duplicates and updates timestamp if new one is greater
		 * @return true if current tally changed, false otherwise
		 */
		bool vote (nano::account const & representative, uint64_t const & timestamp, nano::uint128_t const & rep_weight, std::size_t max_voters);

		/**
		 * Inserts votes stored in this entry into an election
		 */
		std::size_t fill (std::shared_ptr<nano::election> const & election) const;

		std::size_t size () const;
		nano::block_hash hash () const;
		nano::uint128_t tally () const;
		nano::uint128_t final_tally () const;
		std::vector<voter_entry> voters () const;
		std::chrono::steady_clock::time_point last_vote () const;

	private:
		bool vote_impl (nano::account const & representative, uint64_t const & timestamp, nano::uint128_t const & rep_weight, std::size_t max_voters);

		nano::block_hash const hash_m;
		std::vector<voter_entry> voters_m;

		nano::uint128_t tally_m{ 0 };
		nano::uint128_t final_tally_m{ 0 };

		std::chrono::steady_clock::time_point last_vote_m{};
	};

public:
	explicit vote_cache (vote_cache_config const &);

	/**
	 * Adds a new vote to cache
	 */
	void vote (nano::block_hash const & hash, std::shared_ptr<nano::vote> vote);
	/**
	 * Tries to find an entry associated with block hash
	 */
	std::optional<entry> find (nano::block_hash const & hash) const;
	/**
	 * Removes an entry associated with block hash, does nothing if entry does not exist
	 * @return true if hash existed and was erased, false otherwise
	 */
	bool erase (nano::block_hash const & hash);

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
	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);

public:
	/**
	 * Function used to query rep weight for tally calculation
	 */
	std::function<nano::uint128_t (nano::account const &)> rep_weight_query{ [] (nano::account const & rep) { debug_assert (false); return 0; } };

private: // Dependencies
	vote_cache_config const & config;

private:
	void cleanup ();

	// clang-format off
	class tag_sequenced {};
	class tag_hash {};
	class tag_tally {};
	// clang-format on

	// clang-format off
	using ordered_cache = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<entry, nano::block_hash, &entry::hash>>,
		mi::ordered_non_unique<mi::tag<tag_tally>,
			mi::const_mem_fun<entry, nano::uint128_t, &entry::tally>, std::greater<>> // DESC
	>>;
	// clang-format on
	ordered_cache cache;

	mutable nano::mutex mutex;
	nano::interval cleanup_interval;
};
}