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

	bool triggered{ false };
	std::atomic<bool> stopped{ false };

	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;

private: // Dependencies
	node_config & config;
	store & store;
	election_scheduler & scheduler;
};
}