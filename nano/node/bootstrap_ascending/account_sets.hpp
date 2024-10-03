#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap_ascending/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <random>

namespace mi = boost::multi_index;

namespace nano
{
class stats;

namespace bootstrap_ascending
{
	/** This class tracks accounts various account sets which are shared among the multiple bootstrap threads */
	class account_sets
	{
	public:
		account_sets (account_sets_config const &, nano::stats &);

		/**
		 * If an account is not blocked, increase its priority.
		 * If the account does not exist in priority set and is not blocked, inserts a new entry.
		 * Current implementation increases priority by 1.0f each increment
		 */
		void priority_up (nano::account const & account);
		/**
		 * Decreases account priority
		 * Current implementation divides priority by 2.0f and saturates down to 1.0f.
		 */
		void priority_down (nano::account const & account);
		void priority_set (nano::account const & account);

		void block (nano::account const & account, nano::block_hash const & dependency);
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
		void sync_dependencies ();

		/**
		 * Sampling
		 */
		nano::account next_priority (std::function<bool (nano::account const &)> const & filter);
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

			id_t id{ generate_id () }; // Uniformly distributed, used for random querying
			std::chrono::steady_clock::time_point timestamp{};
		};

		struct blocking_entry
		{
			priority_entry original_entry;
			nano::block_hash dependency;
			nano::account dependency_account{ 0 };

			id_t id{ generate_id () }; // Uniformly distributed, used for random querying

			nano::account account () const
			{
				return original_entry.account;
			}
			double priority () const
			{
				return original_entry.priority;
			}
		};

		// clang-format off
		class tag_sequenced {};
		class tag_account {};
		class tag_id {};
		class tag_dependency {};
		class tag_dependency_account {};
		class tag_priority {};

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
				mi::const_mem_fun<blocking_entry, nano::account, &blocking_entry::account>>,
			mi::ordered_non_unique<mi::tag<tag_dependency>,
				mi::member<blocking_entry, nano::block_hash, &blocking_entry::dependency>>,
			mi::ordered_non_unique<mi::tag<tag_dependency_account>,
				mi::member<blocking_entry, nano::account, &blocking_entry::dependency_account>>,
			mi::ordered_unique<mi::tag<tag_id>,
				mi::member<blocking_entry, id_t, &blocking_entry::id>>
		>>;
		// clang-format on

		ordered_priorities priorities;
		ordered_blocking blocking;

	public: // Constants
		static double constexpr priority_initial = 2.0;
		static double constexpr priority_increase = 2.0;
		static double constexpr priority_divide = 2.0;
		static double constexpr priority_max = 128.0;
		static double constexpr priority_cutoff = 0.15;

	public:
		using info_t = std::tuple<decltype (blocking), decltype (priorities)>; // <blocking, priorities>
		info_t info () const;
	};
}
}