#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/observer_set.hpp>
#include <celerix/lib/rate_limiting.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/account_info.hpp>
#include <celerix/secure/common.hpp>

#include <condition_variable>
#include <deque>
#include <thread>

namespace celerix
{
class backlog_scan_config final
{
public:
	celerix::error deserialize (celerix::tomlconfig &);
	celerix::error serialize (celerix::tomlconfig &) const;

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
	backlog_scan (backlog_scan_config const &, celerix::ledger &, celerix::stats &);
	~backlog_scan ();

	void start ();
	void stop ();

	/** Manually trigger backlog population */
	void trigger ();

	/** Notify about AEC vacancy */
	void notify ();

	celerix::container_info container_info () const;

public:
	struct activated_info
	{
		celerix::account account;
		celerix::account_info account_info;
		celerix::confirmation_height_info conf_info;
	};

	using batch_event_t = celerix::observer_set<std::deque<activated_info>>;
	batch_event_t batch_scanned; // Accounts scanned but not activated
	batch_event_t batch_activated; // Accounts activated

private: // Dependencies
	backlog_scan_config const & config;
	celerix::ledger & ledger;
	celerix::stats & stats;

private:
	void run ();
	bool predicate () const;
	void populate_backlog (celerix::unique_lock<celerix::mutex> & lock);

private:
	celerix::rate_limiter limiter;

	/** This is a manual trigger, the ongoing backlog population does not use this.
	 *  It can be triggered even when backlog population (frontiers confirmation) is disabled. */
	bool triggered{ false };

	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;

	/** Thread that runs the backlog implementation logic. The thread always runs, even if
	 *  backlog population is disabled, so that it can service a manual trigger (e.g. via RPC). */
	std::thread thread;
};
}
