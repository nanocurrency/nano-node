#pragma once

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

/**
 *	A container holding votes that do not match any active or recently finished elections.
 *	It keeps track of votes in two internal structures: cache and queue
 *
 *	Cache: Stores votes associated with a particular block hash with a bounded maximum number of votes per hash.
 *			When cache size exceeds `max_size` oldest entries are evicted first.
 *
 *	Queue: Keeps track of block hashes ordered by total cached vote tally.
 *			When inserting a new vote into cache, the queue is atomically updated.
 *			When queue size exceeds `max_size` oldest entries are evicted first.
 */
class vote_cache final
{
public:
	class config final
	{
	public:
		std::size_t max_size;
	};

public:
	/**
	 * Class that stores votes associated with a single block hash
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
		constexpr static int max_voters = 80;

		explicit entry (nano::block_hash const & hash);

		/**
		 * Adds a vote into a list, checks for duplicates and updates timestamp if new one is greater
		 * @return true if current tally changed, false otherwise
		 */
		bool vote (nano::account const & representative, uint64_t const & timestamp, nano::uint128_t const & rep_weight);
		/**
		 * Inserts votes stored in this entry into an election
		 */
		std::size_t fill (std::shared_ptr<nano::election> const & election) const;
		/*
		 * Size of this entry
		 */
		std::size_t size () const;

		nano::block_hash hash () const;
		nano::uint128_t tally () const;
		nano::uint128_t final_tally () const;
		std::vector<voter_entry> voters () const;

	private:
		nano::block_hash const hash_m;
		std::vector<voter_entry> voters_m;

		nano::uint128_t tally_m{ 0 };
		nano::uint128_t final_tally_m{ 0 };
	};

private:
	class queue_entry final
	{
	public:
		nano::block_hash hash{ 0 };
		nano::uint128_t tally{ 0 };
	};

public:
	explicit vote_cache (const config);

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
	/**
	 * Returns an entry with the highest tally.
	 * @param min_tally minimum tally threshold, entries below with their voting weight below this will be ignored
	 */
	std::optional<entry> peek (nano::uint128_t const & min_tally = 0) const;
	/**
	 * Returns an entry with the highest tally and removes it from container.
	 * @param min_tally minimum tally threshold, entries below with their voting weight below this will be ignored
	 */
	std::optional<entry> pop (nano::uint128_t const & min_tally = 0);
	/**
	 * Reinserts a block into the queue.
	 * It is possible that we dequeue a hash that doesn't have a received block yet (for eg. if publish message was lost).
	 * We need a way to reinsert that hash into the queue when we finally receive the block
	 */
	void trigger (const nano::block_hash & hash);

	std::size_t cache_size () const;
	std::size_t queue_size () const;
	bool cache_empty () const;
	bool queue_empty () const;

public: // Container info
	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);

public:
	/**
	 * Function used to query rep weight for tally calculation
	 */
	std::function<nano::uint128_t (nano::account const &)> rep_weight_query{ [] (nano::account const & rep) { return 0; } };

private:
	void vote_impl (nano::block_hash const & hash, nano::account const & representative, uint64_t const & timestamp, nano::uint128_t const & rep_weight);
	std::optional<entry> find_locked (nano::block_hash const & hash) const;
	void trim_overflow_locked ();

	const std::size_t max_size;

	// clang-format off
	class tag_sequenced {};
	class tag_tally {};
	class tag_hash {};
	// clang-format on

	// clang-format off
	using ordered_cache = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<entry, nano::block_hash, &entry::hash>>
	>>;
	// clang-format on
	ordered_cache cache;

	// clang-format off
	using ordered_queue = boost::multi_index_container<queue_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::ordered_non_unique<mi::tag<tag_tally>,
			mi::member<queue_entry, nano::uint128_t, &queue_entry::tally>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<queue_entry, nano::block_hash, &queue_entry::hash>>
	>>;
	// clang-format on
	ordered_queue queue;

	mutable nano::mutex mutex;
};
}