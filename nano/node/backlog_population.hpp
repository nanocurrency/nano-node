#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/secure/common.hpp>

#include <atomic>
#include <condition_variable>
#include <thread>

namespace nano
{
class stat;
class store;
class election_scheduler;

class backlog_population final
{
public:
	struct config
	{
		/** Control if ongoing backlog population is enabled. If not, backlog population can still be triggered by RPC */
		bool enabled;

		/** Number of accounts per second to process. Number of accounts per single batch is this value divided by `frequency` */
		unsigned batch_size;

		/** Number of batches to run per second. Batches run in 1 second / `frequency` intervals */
		unsigned frequency;
	};

	backlog_population (const config &, nano::store &, nano::stat &);
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
	using callback_t = nano::observer_set<nano::transaction const &, nano::account const &, nano::account_info const &, nano::confirmation_height_info const &>;
	callback_t activate_callback;

private: // Dependencies
	nano::store & store;
	nano::stat & stats;

	config config_m;

private:
	void run ();
	bool predicate () const;

	void populate_backlog ();
	void activate (nano::transaction const &, nano::account const &);

	/** This is a manual trigger, the ongoing backlog population does not use this.
	 *  It can be triggered even when backlog population (frontiers confirmation) is disabled. */
	bool triggered{ false };

	std::atomic<bool> stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;

	/** Thread that runs the backlog implementation logic. The thread always runs, even if
	 *  backlog population is disabled, so that it can service a manual trigger (e.g. via RPC). */
	std::thread thread;
};
}
