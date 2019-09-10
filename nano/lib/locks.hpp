#pragma once

#include <nano/lib/timer.hpp>
#if NANO_TIMED_LOCKS > 0
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <boost/fiber/condition_variable.hpp>
#endif
#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace nano
{
#if NANO_TIMED_LOCKS > 0
template <typename Mutex>
class lock_guard final
{
public:
	explicit lock_guard (Mutex & mutex_a) :
	guard (mutex_a)
	{
	}

	lock_guard (const lock_guard &) = delete;
	lock_guard & operator= (const lock_guard &) = delete;

private:
	std::lock_guard<Mutex> guard;
};

template <>
class lock_guard<std::mutex> final
{
public:
	explicit lock_guard (std::mutex & mutex_a);
	~lock_guard () noexcept;

	lock_guard (const lock_guard &) = delete;
	lock_guard & operator= (const lock_guard &) = delete;

private:
	std::mutex & mut;
	nano::timer<std::chrono::milliseconds> timer;
};

template <typename Mutex, typename = std::enable_if_t<std::is_same<Mutex, std::mutex>::value>>
class unique_lock final
{
public:
	unique_lock () = default;
	explicit unique_lock (Mutex & mutex_a);
	unique_lock (unique_lock && other) = delete;
	unique_lock & operator= (unique_lock && other) noexcept;
	~unique_lock () noexcept;
	unique_lock (const unique_lock &) = delete;
	unique_lock & operator= (const unique_lock &) = delete;

	void lock ();
	bool try_lock ();
	void unlock ();
	bool owns_lock () const noexcept;
	explicit operator bool () const noexcept;
	Mutex * mutex () const noexcept;

private:
	Mutex * mut{ nullptr };
	bool owns{ false };

	nano::timer<std::chrono::milliseconds> timer;

	void validate () const;
	void lock_impl ();
};

class condition_variable final
{
private:
	boost::fibers::condition_variable_any cnd;

public:
	condition_variable () = default;
	condition_variable (condition_variable const &) = delete;
	condition_variable & operator= (condition_variable const &) = delete;

	void notify_one () noexcept;
	void notify_all () noexcept;
	void wait (nano::unique_lock<std::mutex> & lt);

	template <typename Pred>
	void wait (nano::unique_lock<std::mutex> & lt, Pred pred)
	{
		cnd.wait (lt, pred);
	}

	template <typename Clock, typename Duration>
	void wait_until (nano::unique_lock<std::mutex> & lk, std::chrono::time_point<Clock, Duration> const & timeout_time)
	{
		cnd.wait_until (lk, timeout_time);
	}

	template <typename Clock, typename Duration, typename Pred>
	bool wait_until (nano::unique_lock<std::mutex> & lk, std::chrono::time_point<Clock, Duration> const & timeout_time, Pred pred)
	{
		return cnd.wait_until (lk, timeout_time, pred);
	}

	template <typename Rep, typename Period>
	void wait_for (nano::unique_lock<std::mutex> & lk,
	std::chrono::duration<Rep, Period> const & timeout_duration)
	{
		cnd.wait_for (lk, timeout_duration);
	}

	template <typename Rep, typename Period, typename Pred>
	bool wait_for (nano::unique_lock<std::mutex> & lk, std::chrono::duration<Rep, Period> const & timeout_duration, Pred pred)
	{
		return cnd.wait_for (lk, timeout_duration, pred);
	}
};

#else
template <typename Mutex>
using lock_guard = std::lock_guard<Mutex>;

template <typename Mutex>
using unique_lock = std::unique_lock<Mutex>;

using condition_variable = std::condition_variable;
#endif
}
