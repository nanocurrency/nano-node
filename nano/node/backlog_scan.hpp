#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <deque>
#include <thread>

namespace nano
{
class backlog_scan_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	/** Control if ongoing backlog population is enabled. If not, backlog population can still be triggered by RPC */
	bool enable{ true };
	/** Number of accounts to scan per second. */
	size_t rate_limit{ 10000 };
	/** Number of accounts per second to process. */
	size_t batch_size{ 1000 };
};

class backlog_scan final
{
public:
	backlog_scan (backlog_scan_config const &, nano::ledger &, nano::stats &);
	~backlog_scan ();

	void start ();
	void stop ();

	/** Manually trigger backlog population */
	void trigger ();

	/** Notify about AEC vacancy */
	void notify ();

	nano::container_info container_info () const;

public:
	struct activated_info
	{
		nano::account account;
		nano::account_info account_info;
		nano::confirmation_height_info conf_info;
	};

	using batch_event_t = nano::observer_set<std::deque<activated_info>>;
	batch_event_t batch_scanned; // Accounts scanned but not activated
	batch_event_t batch_activated; // Accounts activated

private: // Dependencies
	backlog_scan_config const & config;
	nano::ledger & ledger;
	nano::stats & stats;

private:
	void run ();
	bool predicate () const;
	void populate_backlog (nano::unique_lock<nano::mutex> & lock);

private:
	nano::rate_limiter limiter;

	/** This is a manual trigger, the ongoing backlog population does not use this.
	 *  It can be triggered even when backlog population (frontiers confirmation) is disabled. */
	bool triggered{ false };

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;

	/** Thread that runs the backlog implementation logic. The thread always runs, even if
	 *  backlog population is disabled, so that it can service a manual trigger (e.g. via RPC). */
	std::thread thread;
};
}
