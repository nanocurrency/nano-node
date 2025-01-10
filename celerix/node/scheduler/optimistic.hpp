#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/common.hpp>

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

namespace celerix::scheduler
{
class optimistic_config final
{
public:
	celerix::error deserialize (celerix::tomlconfig & toml);
	celerix::error serialize (celerix::tomlconfig & toml) const;

public:
	bool enable{ true };

	/** Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation */
	std::size_t gap_threshold{ 32 };

	/** Maximum number of candidates stored in memory */
	std::size_t max_size{ 1024 * 64 };
};

class optimistic final
{
	struct entry;

public:
	optimistic (optimistic_config const &, celerix::node &, celerix::ledger &, celerix::active_elections &, celerix::network_constants const & network_constants, celerix::stats &);
	~optimistic ();

	void start ();
	void stop ();

	/**
	 * Called from backlog population to process accounts with unconfirmed blocks
	 */
	bool activate (celerix::account const &, celerix::account_info const &, celerix::confirmation_height_info const &);

	/**
	 * Notify about changes in AEC vacancy
	 */
	void notify ();

	celerix::container_info container_info () const;

private:
	bool activate_predicate (celerix::account_info const &, celerix::confirmation_height_info const &) const;

	bool predicate () const;
	void run ();
	void run_one (secure::transaction const &, entry const & candidate);

private: // Dependencies
	optimistic_config const & config;
	celerix::node & node;
	celerix::ledger & ledger;
	celerix::active_elections & active;
	celerix::network_constants const & network_constants;
	celerix::stats & stats;

private:
	struct entry
	{
		celerix::account account;
		celerix::clock::time_point timestamp;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_account {};

	using ordered_candidates = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<entry, celerix::account, &entry::account>>
	>>;
	// clang-format on

	/** Accounts eligible for optimistic scheduling */
	ordered_candidates candidates;

	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::thread thread;
};
}
