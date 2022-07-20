#pragma once

#include <nano/lib/locks.hpp>

#include <atomic>
#include <condition_variable>
#include <thread>

namespace nano
{
class node_config;
class store;
class election_scheduler;

class backlog_population final
{
public:
	explicit backlog_population (node_config & config, store & store, election_scheduler & scheduler);
	~backlog_population ();

	void start ();
	void stop ();
	void trigger ();
	void notify ();

private:
	void run ();
	bool predicate () const;

	void populate_backlog ();

	/** This is a manual trigger, the ongoing backlog population does not use this.
	 *  It can be triggered even when backlog population (frontiers confirmation) is disabled. */
	bool triggered{ false };

	std::atomic<bool> stopped{ false };

	nano::condition_variable condition;
	mutable nano::mutex mutex;

	/** Thread that runs the backlog implementation logic. The thread always runs, even if
	 *  backlog population is disabled, so that it can service a manual trigger (e.g. via RPC). */
	std::thread thread;

private: // Dependencies
	node_config & config;
	store & store_m;
	election_scheduler & scheduler;
};
}
