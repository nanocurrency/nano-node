#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

namespace nano
{
class node;
class active_transactions;

class optimistic_scheduler_config final
{
public:
	/** Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation */
	std::size_t gap_threshold{ 32 };

	/** Maximum number of candidates stored in memory */
	std::size_t max_size{ 1024 * 16 };
};

class optimistic_scheduler final
{
public:
	optimistic_scheduler (optimistic_scheduler_config const &, nano::node &, nano::active_transactions &, nano::stats &);
	~optimistic_scheduler ();

	void start ();
	void stop ();

	/**
	 * Called from backlog population to process not yet confirmed blocks
	 * Flow: backlog_population frontier scan > election_scheduler::activate > (gather account info) > optimistic_scheduler::activate
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
	void run_one (nano::account candidate);

private: // Dependencies
	optimistic_scheduler_config const & config;
	nano::node & node;
	nano::active_transactions & active;
	nano::stats & stats;

private:
	/** Accounts eligible for optimistic scheduling */
	std::deque<nano::account> candidates;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}