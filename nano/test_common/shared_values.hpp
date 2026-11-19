#pragma once

#include <nano/lib/locks.hpp>

#include <atomic>
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

	void increment () const
	{
		counter->fetch_add (1);
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
