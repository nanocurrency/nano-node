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
class account_info;
class active_transactions;
class ledger;
class node;
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
	bool activate (nano::secure::transaction const & transaction, nano::account const & account);

	/**
	 * Notify about changes in AEC vacancy
	 */
	void notify ();

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

private:
	bool activate_predicate (nano::secure::transaction const & transaction, nano::account const & account) const;

	bool predicate () const;
	void run ();
	void run_one (secure::transaction const &, entry const & candidate);

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
