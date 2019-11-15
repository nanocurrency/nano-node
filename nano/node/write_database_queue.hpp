#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace nano
{
/** Distinct areas write locking is done, order is irrelevant */
enum class writer
{
	confirmation_height,
	process_batch,
	testing // Used in tests to emulate a write lock
};

class write_guard final
{
public:
	write_guard (nano::condition_variable & cv_a, std::function<void()> guard_finish_callback_a);
	~write_guard ();

private:
	nano::condition_variable & cv;
	std::function<void()> guard_finish_callback;
};

class write_database_queue final
{
public:
	write_database_queue ();
	/** Blocks until we are at the head of the queue */
	write_guard wait (nano::writer writer);

	/** Returns true if this writer is now at the front of the queue */
	bool process (nano::writer writer);

	/** Returns true if this writer is anywhere in the queue */
	bool contains (nano::writer writer);

	/** Doesn't actually pop anything until the returned write_guard is out of scope */
	write_guard pop ();

	/** This will release anything which is being blocked by the wait function */
	void stop ();

private:
	std::deque<nano::writer> queue;
	std::mutex mutex;
	nano::condition_variable cv;
	std::function<void()> guard_finish_callback;
	std::atomic<bool> stopped{ false };
};
}
