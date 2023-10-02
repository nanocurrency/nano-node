#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
class node;
class ledger;
class active_transactions;
}

namespace nano::scheduler
{
class optimistic_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	bool enabled{ true };

	/** Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation */
	std::size_t gap_threshold{ 32 };

	/** Maximum number of candidates stored in memory */
	std::size_t max_size{ 1024 * 64 };
};

class optimistic final
{
	struct entry;

public:
	optimistic (optimistic_config const &, nano::node &, nano::ledger &, nano::active_transactions &, nano::network_constants const & network_constants, nano::stats &);
	~optimistic ();

	void start ();
	void stop ();

	/**
	 * Called from backlog population to process accounts with unconfirmed blocks
	 */
	bool activate (nano::account const &, nano::account_info const &, nano::confirmation_height_info const &);

	/**
	 * Notify about changes in AEC vacancy
	 */
	void notify ();

private:
	bool activate_predicate (nano::account_info const &, nano::confirmation_height_info const &) const;

	bool predicate () const;
	void run ();
	void run_one (store::transaction const &, entry const & candidate);

private: // Dependencies
	optimistic_config const & config;
	nano::node & node;
	nano::ledger & ledger;
	nano::active_transactions & active;
	nano::network_constants const & network_constants;
	nano::stats & stats;

private:
	struct entry
	{
		nano::account account;
		nano::clock::time_point timestamp;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_account {};

	using ordered_candidates = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<entry, nano::account, &entry::account>>
	>>;
	// clang-format on

	/** Accounts eligible for optimistic scheduling */
	ordered_candidates candidates;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
