#pragma once

#include <nano/lib/locks.hpp>

#include <atomic>
#include <condition_variable>
#include <thread>

namespace nano
{
class node;

class backlog_population final
{
public:
	explicit backlog_population (nano::node & node);
	~backlog_population ();

	void start ();
	void stop ();
	void trigger ();
	void notify ();

private:
	void run ();
	bool predicate () const;

	void populate_backlog ();

	nano::node & node;

	bool triggered{ false };
	std::atomic<bool> stopped{ false };

	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}