#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <memory>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
class node;
class vote;
class election;

/**
 *	A container holding votes that do not match any active or recently finished elections.
 */
class vote_cache final
{
public:
	/**
	 * Class that stores votes associated with a single block hash
	 */
	class entry final
	{
	public:
		constexpr static int max_voters = 40;

		explicit entry (nano::block_hash const & hash);

		nano::block_hash hash;
		std::vector<std::pair<nano::account, uint64_t>> voters; // <rep, timestamp> pair
		nano::uint128_t tally{ 0 };

		/**
		 * Adds a vote into a list, checks for duplicates and updates timestamp if new one is greater
		 * @return true if current tally changed, false otherwise
		 */
		bool vote (nano::account const & representative, uint64_t const & timestamp, nano::uint128_t const & rep_weight);
		/**
		 * Inserts votes stored in this entry into an election
		 */
		void fill (std::shared_ptr<nano::election> const election) const;
	};

private:
	class queue_entry final
	{
	public:
		nano::block_hash hash{ 0 };
		nano::uint128_t tally{ 0 };
	};

public:
	explicit vote_cache (std::size_t max_size);

	/**
	 * Adds a new vote to cache
	 */
	void vote (nano::block_hash const & hash, std::shared_ptr<nano::vote> const vote);
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
	 */
	std::optional<entry> peek (nano::uint128_t const & min_tally = 0) const;
	/**
	 * Returns an entry with the highest tally and removes it from container.
	 */
	std::optional<entry> pop (nano::uint128_t const & min_tally = 0);
	void trigger (const nano::block_hash & hash);

	std::size_t size () const;
	bool empty () const;
	bool queue_empty () const;

public:
	/**
	 * Function used to query rep weight for tally calculation
	 */
	std::function<nano::uint128_t (nano::account const &)> rep_weight_query{ [] (nano::account const & rep) { return 0; } };

private:
	void vote_impl_locked (nano::block_hash const & hash, nano::account const & representative, uint64_t const & timestamp, nano::uint128_t const & rep_weight);
	std::optional<entry> find_locked (nano::block_hash const & hash) const;
	void trim_overflow_locked ();

	std::size_t max_size = 1024 * 128;

	// clang-format off
	class tag_random_access {};
	class tag_tally {};
	class tag_hash {};
	// clang-format on

	// clang-format off
	using ordered_cache = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::random_access<mi::tag<tag_random_access>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, nano::block_hash, &entry::hash>>>>;
	// clang-format on
	ordered_cache cache;

	// clang-format off
	using ordered_queue = boost::multi_index_container<queue_entry,
	mi::indexed_by<
		mi::random_access<mi::tag<tag_random_access>>,
		mi::ordered_non_unique<mi::tag<tag_tally>,
			mi::member<queue_entry, nano::uint128_t, &queue_entry::tally>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<queue_entry, nano::block_hash, &queue_entry::hash>>>>;
	// clang-format on
	ordered_queue queue;

	mutable nano::mutex mutex;
};
}