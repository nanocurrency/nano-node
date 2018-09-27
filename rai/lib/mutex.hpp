#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace rai
{
#ifndef NDEBUG
#define RAI_DEADLOCK_DETECTION

size_t create_resource_lock_id ();
void notify_resource_locking (size_t);
void notify_resource_unlocking (size_t);
void destroy_resource_lock_id (size_t);

class condition_variable;

class mutex : std::mutex
{
	friend condition_variable;

public:
	mutex () noexcept;
    ~mutex ();
	void lock ();
	bool try_lock ();
	void unlock ();

protected:
	size_t resource_lock_id;
};

class condition_variable : std::condition_variable
{
public:
	condition_variable ();
	void notify_one () noexcept;
	void notify_all () noexcept;
	void wait (std::unique_lock<rai::mutex> &);
	template <class Predicate>
	void wait (std::unique_lock<rai::mutex> & lock, Predicate pred)
    {
        std::unique_lock<std::mutex> std_lock (static_cast<std::mutex &> (*lock.mutex ()), std::adopt_lock);
        std::condition_variable::wait (std_lock, pred);
        std_lock.release ();
    }
	template <class Rep, class Period>
	void wait_for (std::unique_lock<rai::mutex> & lock, const std::chrono::duration<Rep, Period> & rel_time)
    {
        std::unique_lock<std::mutex> std_lock (static_cast<std::mutex &> (*lock.mutex ()), std::adopt_lock);
        std::condition_variable::wait_for (std_lock, rel_time);
        std_lock.release ();
    }
	template <class Rep, class Period, class Predicate>
	void wait_for (std::unique_lock<rai::mutex> & lock, const std::chrono::duration<Rep, Period> & rel_time, Predicate pred)
    {
        std::unique_lock<std::mutex> std_lock (static_cast<std::mutex &> (*lock.mutex ()), std::adopt_lock);
        std::condition_variable::wait_for (std_lock, rel_time, pred);
        std_lock.release ();
    }
	template <class Clock, class Duration>
	void wait_until (std::unique_lock<rai::mutex> & lock, const std::chrono::time_point<Clock, Duration> & abs_time)
    {
        std::unique_lock<std::mutex> std_lock (static_cast<std::mutex &> (*lock.mutex ()), std::adopt_lock);
        std::condition_variable::wait_until (std_lock, abs_time);
        std_lock.release ();
    }
	template <class Clock, class Duration, class Predicate>
	void wait_until (std::unique_lock<rai::mutex> & lock, const std::chrono::time_point<Clock, Duration> & abs_time, Predicate pred)
    {
        std::unique_lock<std::mutex> std_lock (static_cast<std::mutex &> (*lock.mutex ()), std::adopt_lock);
        std::condition_variable::wait_until (std_lock, abs_time, pred);
        std_lock.release ();
    }
};
#else
typedef std::mutex mutex;
typedef std::condition_variable condition_variable;
#endif
}
