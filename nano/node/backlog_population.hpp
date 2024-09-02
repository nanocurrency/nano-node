#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <thread>

namespace nano::secure
{
class transaction;
}
namespace nano
{
class account_info;
class election_scheduler;
class ledger;
class stats;

class backlog_population_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	/** Control if ongoing backlog population is enabled. If not, backlog population can still be triggered by RPC */
	bool enable{ true };
	/** Number of accounts per second to process. Number of accounts per single batch is this value divided by `frequency` */
	unsigned batch_size{ 10 * 1000 };
	/** Number of batches to run per second. Batches run in 1 second / `frequency` intervals */
	unsigned frequency{ 10 };
};

class backlog_population final
{
public:
	backlog_population (backlog_population_config const &, nano::scheduler::component &, nano::ledger &, nano::stats &);
	~backlog_population ();

	void start ();
	void stop ();

	/** Manually trigger backlog population */
	void trigger ();

	/** Notify about AEC vacancy */
	void notify ();

public:
	/**
	 * Callback called for each backlogged account
	 */
	using callback_t = nano::observer_set<secure::transaction const &, nano::account const &>;
	callback_t activate_callback;

private: // Dependencies
	backlog_population_config const & config;
	nano::scheduler::component & schedulers;
	nano::ledger & ledger;
	nano::stats & stats;

private:
	void run ();
	bool predicate () const;
	void populate_backlog (nano::unique_lock<nano::mutex> & lock);
	void activate (secure::transaction const &, nano::account const &, nano::account_info const &);

private:
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
