#pragma once

#include <nano/lib/locks.hpp>

#include <condition_variable>
#include <deque>
#include <functional>

namespace nano
{
/** Distinct areas write locking is done, order is irrelevant */
enum class writer
{
	confirmation_height,
	process_batch,
	pruning,
	testing // Used in tests to emulate a write lock
};

class write_guard final
{
public:
	write_guard (std::function<void ()> guard_finish_callback_a);
	void release ();
	~write_guard ();
	write_guard (write_guard const &) = delete;
	write_guard & operator= (write_guard const &) = delete;
	write_guard (write_guard &&) noexcept;
	write_guard & operator= (write_guard &&) noexcept;
	bool is_owned () const;

private:
	std::function<void ()> guard_finish_callback;
	bool owns{ true };
};

class write_database_queue final
{
public:
	write_database_queue (bool use_noops_a);
	/** Blocks until we are at the head of the queue */
	write_guard wait (nano::writer writer);

	/** Returns true if this writer is now at the front of the queue */
	bool process (nano::writer writer);

	/** Returns true if this writer is anywhere in the queue. Currently only used in tests */
	bool contains (nano::writer writer);

	/** Doesn't actually pop anything until the returned write_guard is out of scope */
	write_guard pop ();

private:
	std::deque<nano::writer> queue;
	nano::mutex mutex;
	nano::condition_variable cv;
	std::function<void ()> guard_finish_callback;
	bool use_noops;
};
}
