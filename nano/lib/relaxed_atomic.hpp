#pragma once

#include <atomic>
#include <type_traits>

namespace nano
{
/* Default memory order of normal std::atomic operations is std::memory_order_seq_cst which provides
   a total global ordering of atomic operations as well as synchronization between threads. Weaker memory
   ordering can provide benefits in some circumstances, like dumb counters where no other data is
   dependent on the ordering of these operations. This assumes T is a type of integer, not bool or char. */
template <typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
class relaxed_atomic_integral
{
public:
	relaxed_atomic_integral () noexcept = default;
	constexpr relaxed_atomic_integral (T desired) noexcept :
		atomic (desired)
	{
	}

	T operator= (T desired) noexcept
	{
		store (desired);
		return atomic;
	}

	relaxed_atomic_integral (relaxed_atomic_integral const &) = delete;
	relaxed_atomic_integral & operator= (relaxed_atomic_integral const &) = delete;

	void store (T desired, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		atomic.store (desired, order);
	}

	T load (std::memory_order order = std::memory_order_relaxed) const noexcept
	{
		return atomic.load (std::memory_order_relaxed);
	}

	operator T () const noexcept
	{
		return load ();
	}

	bool compare_exchange_weak (T & expected, T desired, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		return atomic.compare_exchange_weak (expected, desired, order);
	}

	bool compare_exchange_strong (T & expected, T desired, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		return atomic.compare_exchange_strong (expected, desired, order);
	}

	T fetch_add (T arg, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		return atomic.fetch_add (arg, order);
	}

	T fetch_sub (T arg, std::memory_order order = std::memory_order_relaxed) noexcept
	{
		return atomic.fetch_sub (arg, order);
	}

	T operator++ () noexcept
	{
		return fetch_add (1) + 1;
	}

	T operator++ (int) noexcept
	{
		return fetch_add (1);
	}

	T operator-- () noexcept
	{
		return fetch_sub (1) - 1;
	}

	T operator-- (int) noexcept
	{
		return fetch_sub (1);
	}

private:
	std::atomic<T> atomic;
};
}