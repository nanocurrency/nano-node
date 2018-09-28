#include <rai/lib/mutex.hpp>

#ifdef RAI_DEADLOCK_DETECTION

#include <atomic>
#include <boost/stacktrace.hpp>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>

template <class T>
class movable_atomic : public std::atomic<T>
{
public:
	movable_atomic (T inner) :
	std::atomic<T> (inner)
	{
	}

	movable_atomic (movable_atomic & other) :
	std::atomic<T> (static_cast<T> (other))
	{
	}
};

class lock_info
{
public:
	std::vector<movable_atomic<boost::stacktrace::stacktrace *>> locked_after;
	boost::stacktrace::stacktrace creation_backtrace;
};

static std::vector<lock_info> locks_info;
static std::vector<size_t> free_lock_ids;
static std::shared_timed_mutex lock_info_mutex;

thread_local std::vector<std::pair<size_t, boost::stacktrace::stacktrace>> thread_has_locks;

size_t rai::create_resource_lock_id ()
{
	std::unique_lock<std::shared_timed_mutex> locks_info_guard (lock_info_mutex);
	size_t id;
	if (free_lock_ids.empty ())
	{
		id = locks_info.size ();
		for (auto & lock_info : locks_info)
		{
			lock_info.locked_after.push_back (movable_atomic<boost::stacktrace::stacktrace *> (nullptr));
		}
		std::vector<movable_atomic<boost::stacktrace::stacktrace *>> locked_after (locks_info.size () + 1, movable_atomic<boost::stacktrace::stacktrace *> (nullptr));
		locks_info.push_back (lock_info{ locked_after, boost::stacktrace::stacktrace () });
	}
	else
	{
		id = free_lock_ids.back ();
		free_lock_ids.pop_back ();
		for (auto & locked_after : locks_info[id].locked_after)
		{
			locked_after.store (nullptr, std::memory_order_relaxed);
		}
		locks_info[id].creation_backtrace = boost::stacktrace::stacktrace ();
	}
	return id;
	return 0;
}

void rai::notify_resource_locking (size_t id)
{
	std::shared_lock<std::shared_timed_mutex> locks_info_guard (lock_info_mutex);
	auto & lock_info (locks_info[id]);
	for (auto & locked_after : thread_has_locks)
	{
		auto other_id (locked_after.first);
		if (!lock_info.locked_after[other_id].load (std::memory_order_relaxed))
		{
			boost::stacktrace::stacktrace * backtrace_alloc (new boost::stacktrace::stacktrace (locked_after.second));
			boost::stacktrace::stacktrace * expected (nullptr);
			if (lock_info.locked_after[other_id].compare_exchange_strong (expected, backtrace_alloc, std::memory_order_acq_rel))
			{
				auto other_backtrace (locks_info[other_id].locked_after[id].load (std::memory_order_acq_rel));
				if (other_backtrace)
				{
					std::cerr << "Potential deadlock detected between resource ids " << id << " and " << other_id << std::endl;
					std::cerr << std::endl;
					std::cerr << "Resource id " << id << " creation backtrace:" << std::endl;
					std::cerr << lock_info.creation_backtrace << std::endl;
					std::cerr << "Resource id " << other_id << " creation backtrace:" << std::endl;
					std::cerr << locks_info[other_id].creation_backtrace << std::endl;
					std::cerr << "Backtrace of " << id << " -> " << other_id << " locking:" << std::endl;
					std::cerr << *other_backtrace << std::endl;
					std::cerr << "Backtrace of " << other_id << " -> " << id << " locking:" << std::endl;
					std::cerr << locked_after.second << std::endl;
					abort ();
				}
			}
			else
			{
				delete backtrace_alloc;
			}
		}
	}
	thread_has_locks.push_back (std::make_pair (id, boost::stacktrace::stacktrace ()));
}

void rai::notify_resource_unlocking (size_t id)
{
	auto it (thread_has_locks.end ());
	auto end (thread_has_locks.begin ());
	while (it != end)
	{
		--it;
		if (it->first == id)
		{
			thread_has_locks.erase (it);
			break;
		}
	}
}

void rai::destroy_resource_lock_id (size_t id)
{
	std::unique_lock<std::shared_timed_mutex> locks_info_guard (lock_info_mutex);
	free_lock_ids.push_back (id);
	for (auto & lock_info : locks_info)
	{
		lock_info.locked_after[id].store(nullptr, std::memory_order_relaxed);
	}
}

rai::mutex::mutex () noexcept :
std::mutex (),
resource_lock_id (create_resource_lock_id ())
{
}

rai::mutex::~mutex ()
{
	rai::destroy_resource_lock_id (resource_lock_id);
}

void rai::mutex::lock ()
{
	rai::notify_resource_locking (resource_lock_id);
	std::mutex::lock ();
}

bool rai::mutex::try_lock ()
{
	auto locked (std::mutex::try_lock ());
	if (locked)
	{
		rai::notify_resource_locking (resource_lock_id);
	}
	return locked;
}

void rai::mutex::unlock ()
{
	rai::notify_resource_unlocking (resource_lock_id);
	std::mutex::unlock ();
}

rai::condition_variable::condition_variable () :
std::condition_variable ()
{
}

void rai::condition_variable::notify_one () noexcept
{
	std::condition_variable::notify_one ();
}

void rai::condition_variable::notify_all () noexcept
{
	std::condition_variable::notify_all ();
}

void rai::condition_variable::wait (std::unique_lock<rai::mutex> & lock)
{
	std::unique_lock<std::mutex> std_lock (static_cast<std::mutex &> (*lock.mutex ()), std::adopt_lock);
	std::condition_variable::wait (std_lock);
	std_lock.release ();
}

#endif
