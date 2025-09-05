#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/fwd.hpp>
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

namespace nano::scheduler
{
class optimistic_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	bool enable{ true };

	/** Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation */
	uint64_t gap_threshold{ 16 };

	/** Maximum number of candidates stored in memory */
	std::size_t max_size{ 1024 * 4 };

	/** How much to delay activation of optimistic elections to avoid interfering with election scheduler */
	std::chrono::milliseconds activation_delay{ 1s };
};

class optimistic final
{
	struct entry;

public:
	optimistic (optimistic_config const &, nano::node &, nano::ledger &, nano::active_elections &, nano::network_constants const & network_constants, nano::stats &);
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

	nano::container_info container_info () const;

private:
	bool activate_predicate (nano::account_info const &, nano::confirmation_height_info const &) const;

	bool predicate () const;
	void run ();
	void run_iterative (nano::unique_lock<nano::mutex> &);
	bool run_one (secure::transaction const &, entry const & candidate);

	std::deque<entry> snapshot (size_t max_count) const;

private: // Dependencies
	optimistic_config const & config;
	nano::node & node;
	nano::ledger & ledger;
	nano::active_elections & active;
	nano::network_constants const & network_constants;
	nano::stats & stats;

private:
	struct entry
	{
		nano::account account;
		uint64_t unconfirmed_height;
		std::chrono::steady_clock::time_point timestamp;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_account {};
	class tag_unconfirmed_height {};

	using ordered_candidates = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<entry, nano::account, &entry::account>>,
		mi::ordered_non_unique<mi::tag<tag_unconfirmed_height>,
			mi::member<entry, uint64_t, &entry::unconfirmed_height>, std::greater<>> // Descending
	>>;
	// clang-format on

	/** Accounts eligible for optimistic scheduling */
	ordered_candidates candidates;

	std::atomic<bool> stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
