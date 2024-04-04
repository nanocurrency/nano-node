#pragma once

#include <nano/lib/locks.hpp>

#include <condition_variable>
#include <deque>
#include <functional>

namespace nano::store
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

/**
 * Allocates database write access in a fair maner rather than directly waiting for mutex aquisition
 * Users should wait() for access to database write transaction and hold the write_guard until complete
 */
class write_database_queue final
{
public:
	write_database_queue (bool use_noops_a);
	/** Blocks until we are at the head of the queue and blocks other waiters until write_guard goes out of scope */
	[[nodiscard ("write_guard blocks other waiters")]] write_guard wait (writer writer);

	/** Returns true if this writer is now at the front of the queue */
	bool process (writer writer);

	/** Returns true if this writer is anywhere in the queue. Currently only used in tests */
	bool contains (writer writer);

	/** Doesn't actually pop anything until the returned write_guard is out of scope */
	write_guard pop ();

private:
	std::deque<writer> queue;
	nano::mutex mutex;
	nano::condition_variable cv;
	std::function<void ()> guard_finish_callback;
	bool use_noops;
};
} // namespace nano::store
