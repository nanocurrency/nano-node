#pragma once

#include <nano/lib/locks.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <tuple>

namespace nano::test
{
/**
 * A flag shared by all its copies, for callbacks that can still run after the test body returns, such as observers of a node that only stops when `system` is destroyed.
 * Capture it by value, a reference capture dangles like any other local variable.
 */
class shared_flag final
{
public:
	shared_flag () = default;
	shared_flag (shared_flag const &) = default; // Also serves moves, so a moved-from copy still holds the flag
	shared_flag & operator= (shared_flag const &) = delete; // Would rebind a copy to another flag

	void set () const
	{
		flag->store (true);
	}

	bool is_set () const
	{
		return flag->load ();
	}

private:
	std::shared_ptr<std::atomic<bool>> flag{ std::make_shared<std::atomic<bool>> (false) }; // Never null
};

/**
 * A counter shared by all its copies, for callbacks that can still run after the test body returns, such as observers of a node that only stops when `system` is destroyed.
 * Capture it by value, a reference capture dangles like any other local variable.
 */
class shared_counter final
{
public:
	shared_counter () = default;
	shared_counter (shared_counter const &) = default; // Also serves moves, so a moved-from copy still holds the counter
	shared_counter & operator= (shared_counter const &) = delete; // Would rebind a copy to another counter

	void inc () const
	{
		counter->fetch_add (1);
	}

	void sub () const
	{
		counter->fetch_sub (1);
	}

	std::size_t value () const
	{
		return counter->load ();
	}

private:
	std::shared_ptr<std::atomic<std::size_t>> counter{ std::make_shared<std::atomic<std::size_t>> (0) }; // Never null
};

/**
 * Values a callback reports, shared by all copies, for callbacks that can still run after the test body returns.
 * Capture it by value, a reference capture dangles like any other local variable.
 */
template <typename... Values>
class shared_result final
{
public:
	shared_result () = default;
	shared_result (shared_result const &) = default; // Also serves moves, so a moved-from copy still holds the result
	shared_result & operator= (shared_result const &) = delete; // Would rebind a copy to another result

	// Records the values, replacing any recorded before
	void set (Values const &... values) const;
	// Whether values have been recorded yet
	bool is_set () const;
	// The recorded values, throws `std::bad_optional_access` while nothing has been recorded
	std::tuple<Values...> get () const;

private:
	// Written by the callback's thread and read by the test's, so held under its own mutex
	struct state
	{
		nano::mutex mutex;
		std::optional<std::tuple<Values...>> values;
	};

	std::shared_ptr<state> result{ std::make_shared<state> () }; // Never null
};

/**
 * A value under its own mutex, shared by all its copies, for callbacks that can still run after the test body returns and collect into it, such as observers of a node that only stops when `system` is destroyed.
 * Capture it by value, a reference capture dangles like any other local variable.
 */
template <typename T>
class shared_locked final
{
public:
	shared_locked () = default;
	shared_locked (shared_locked const &) = default; // Also serves moves, so a moved-from copy still holds the value
	shared_locked & operator= (shared_locked const &) = delete; // Would rebind a copy to another value

	// Locks the value for the one call made through the returned guard
	typename nano::locked<T>::scoped_lock operator->() const;
	// Locks the value until the returned guard goes out of scope
	typename nano::locked<T>::scoped_lock lock () const;

private:
	std::shared_ptr<nano::locked<T>> value{ std::make_shared<nano::locked<T>> () }; // Never null
};

/**
 * A gate shared by all its copies, holding the callbacks that wait on it until the test opens it, such as an observer that parks a node thread at a known point.
 * Capture it by value. A wait gives up after its timeout, so a test that fails before opening the gate does not hang the node's teardown.
 */
class shared_gate final
{
public:
	// Longest a wait holds its caller by default, well past the time a test keeps a callback parked
	static std::chrono::seconds constexpr default_timeout{ 30 };

	shared_gate () = default;
	shared_gate (shared_gate const &) = default; // Also serves moves, so a moved-from copy still holds the gate
	shared_gate & operator= (shared_gate const &) = delete; // Would rebind a copy to another gate

	// Blocks until the gate is opened or the timeout, scaled like every test deadline, passes; returns whether it was opened
	bool wait (std::chrono::milliseconds timeout = default_timeout) const;
	// Opens the gate for every current and later caller of wait
	void open () const;
	// Number of callers blocked in wait right now
	std::size_t waiting () const;

private:
	// Opened by the test's thread and waited on by the callbacks', so held under its own mutex
	struct state
	{
		nano::mutex mutex;
		nano::condition_variable condition;
		bool opened{ false };
		std::size_t waiting{ 0 };
	};

	std::shared_ptr<state> gate{ std::make_shared<state> () }; // Never null
};
}

template <typename... Values>
void nano::test::shared_result<Values...>::set (Values const &... values) const
{
	nano::lock_guard<nano::mutex> guard{ result->mutex };
	result->values.emplace (values...);
}

template <typename... Values>
bool nano::test::shared_result<Values...>::is_set () const
{
	nano::lock_guard<nano::mutex> guard{ result->mutex };
	return result->values.has_value ();
}

template <typename... Values>
std::tuple<Values...> nano::test::shared_result<Values...>::get () const
{
	nano::lock_guard<nano::mutex> guard{ result->mutex };
	return result->values.value ();
}

template <typename T>
typename nano::locked<T>::scoped_lock nano::test::shared_locked<T>::operator->() const
{
	return value->operator->();
}

template <typename T>
typename nano::locked<T>::scoped_lock nano::test::shared_locked<T>::lock () const
{
	return value->lock ();
}
