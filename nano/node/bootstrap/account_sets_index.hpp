#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/common.hpp>
#include <nano/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <deque>
#include <random>

namespace mi = boost::multi_index;

namespace nano::bootstrap
{
/** This class tracks accounts various account sets which are shared among the multiple bootstrap threads */
class account_sets_index
{
public: // Constants
	static double constexpr priority_initial = 2.0;
	static double constexpr priority_increase = 2.0;
	static double constexpr priority_divide = 2.0;
	static double constexpr priority_max = 128.0;
	static double constexpr priority_cutoff = 0.15;
	static unsigned constexpr max_fails = 3;

public:
	account_sets_index (account_sets_config const &, nano::stats &);

	void reset ();

	void priority_up (nano::account const & account);
	void priority_down (nano::account const & account);
	void priority_set (nano::account const & account, double priority = priority_initial);
	void priority_erase (nano::account const & account);

	void block (nano::account const & account, nano::block_hash const & dependency, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());
	void unblock (nano::account const & account, std::optional<nano::block_hash> const & hash = std::nullopt);

	void timestamp_set (nano::account const & account);
	void timestamp_reset (nano::account const & account);

	/**
	 * Sets information about the account chain that contains the block hash
	 */
	void dependency_update (nano::block_hash const & hash, nano::account const & dependency_account);

	/**
	 * Should be called periodically to reinsert missing dependencies into the priority set
	 */
	std::size_t sync_dependencies ();

	/**
	 * Should be called periodically to remove old entries from the blocking set
	 */
	size_t decay_blocking (std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());

	struct priority_result
	{
		nano::account account;
		double priority;
		unsigned fails;
	};

	/**
	 * Sampling
	 */
	// Returns up to max_count eligible priority entries, ordered by descending priority
	std::deque<priority_result> next_priority_batch (std::function<bool (nano::account const &)> const & filter, std::size_t max_count);
	nano::block_hash next_blocking (std::function<bool (nano::block_hash const &)> const & filter);

	bool blocked (nano::account const & account) const;
	bool prioritized (nano::account const & account) const;
	// Accounts in the ledger but not in priority list are assumed priority 1.0f
	// Blocked accounts are assumed priority 0.0f
	double priority (nano::account const & account) const;

	std::size_t priority_size () const;
	std::size_t blocked_size () const;
	bool priority_half_full () const;
	bool blocked_half_full () const;

	nano::container_info container_info () const;

private: // Dependencies
	account_sets_config const & config;
	nano::stats & stats;

private:
	void trim_overflow ();

private:
	struct priority_entry
	{
		nano::account account;
		double priority;

		unsigned fails{ 0 };
		std::chrono::steady_clock::time_point timestamp{}; // Use for cooldown, set to current time when this account is sampled
		id_t id{ generate_id () }; // Uniformly distributed, used for random querying
	};

	struct blocking_entry
	{
		nano::account account;
		nano::block_hash dependency;
		std::chrono::steady_clock::time_point timestamp; // Used for decaying old entries

		nano::account dependency_account{ 0 }; // Account that contains the dependency block, fetched via a background dependency walker
		id_t id{ generate_id () }; // Uniformly distributed, used for random querying
	};

	// clang-format off
	class tag_sequenced {};
	class tag_account {};
	class tag_id {};
	class tag_dependency {};
	class tag_dependency_account {};
	class tag_priority {};
	class tag_timestamp {};

	// Tracks the ongoing account priorities
	using ordered_priorities = boost::multi_index_container<priority_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::ordered_unique<mi::tag<tag_account>,
			mi::member<priority_entry, nano::account, &priority_entry::account>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::member<priority_entry, double, &priority_entry::priority>, std::greater<>>, // Descending
		mi::ordered_unique<mi::tag<tag_id>,
			mi::member<priority_entry, id_t, &priority_entry::id>>
	>>;

	// A blocked account is an account that has failed to insert a new block because the source block is not currently present in the ledger
	// An account is unblocked once it has a block successfully inserted
	using ordered_blocking = boost::multi_index_container<blocking_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::ordered_unique<mi::tag<tag_account>,
			mi::member<blocking_entry, nano::account, &blocking_entry::account>>,
		mi::ordered_non_unique<mi::tag<tag_dependency>,
			mi::member<blocking_entry, nano::block_hash, &blocking_entry::dependency>>,
		mi::ordered_non_unique<mi::tag<tag_dependency_account>,
			mi::member<blocking_entry, nano::account, &blocking_entry::dependency_account>>,
		mi::ordered_unique<mi::tag<tag_id>,
			mi::member<blocking_entry, id_t, &blocking_entry::id>>,
		mi::ordered_non_unique<mi::tag<tag_timestamp>,
			mi::member<blocking_entry, std::chrono::steady_clock::time_point, &blocking_entry::timestamp>>
	>>;
	// clang-format on

	ordered_priorities priorities;
	ordered_blocking blocking;

public:
	using info_t = std::tuple<decltype (blocking), decltype (priorities)>; // <blocking, priorities>
	info_t info () const;
};
}
