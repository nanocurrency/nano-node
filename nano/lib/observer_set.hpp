#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/utility.hpp>

#include <functional>
#include <vector>

namespace nano
{
template <typename... T>
class observer_set final
{
public:
	using observer_type = std::function<void (T const &...)>;

public:
	void add (observer_type observer)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		observers.push_back (observer);
	}

	void notify (T const &... args) const
	{
		// Make observers copy to allow parallel notifications
		nano::unique_lock<nano::mutex> lock{ mutex };
		auto observers_copy = observers;
		lock.unlock ();

		for (auto const & observer : observers_copy)
		{
			observer (args...);
		}
	}

	bool empty () const
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return observers.empty ();
	}

	size_t size () const
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return observers.size ();
	}

	nano::container_info container_info () const
	{
		nano::unique_lock<nano::mutex> lock{ mutex };

		nano::container_info info;
		info.put ("observers", observers);
		return info;
	}

private:
	mutable nano::mutex mutex{ mutex_identifier (mutexes::observer_set) };
	std::vector<observer_type> observers;
};

}
