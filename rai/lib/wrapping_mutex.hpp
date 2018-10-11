#pragma once

#include <mutex>

namespace rai
{
template <typename T>
class wrapping_mutex;

template <typename T>
class wrapping_mutex_guard
{
friend wrapping_mutex<T>;
public:
	wrapping_mutex_guard () = delete;

	T * operator-> () const noexcept
	{
		return inner;
	}

	T & operator* () const noexcept
	{
		return *inner;
	}

protected:
	wrapping_mutex_guard (T * inner_a, std::unique_lock<std::mutex> guard_a) :
	inner (inner_a),
	guard (std::move (guard_a))
	{
	}

	T * inner;
	std::unique_lock<std::mutex> guard;
};

template <typename T>
class wrapping_mutex
{
public:
	wrapping_mutex (T inner_a) :
	inner (std::move (inner_a))
	{
	}

	wrapping_mutex_guard<T> lock ()
	{
		return wrapping_mutex_guard<T> (&inner, std::unique_lock<std::mutex> (mutex));
	}

protected:
	std::mutex mutex;
	T inner;
};
}
