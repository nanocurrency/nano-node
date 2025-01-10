#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/utility.hpp>

#include <functional>
#include <vector>

namespace celerix
{
template <typename... T>
class observer_set final
{
public:
	using observer_type = std::function<void (T const &...)>;

public:
	void add (observer_type observer)
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		observers.push_back (observer);
	}

	void notify (T const &... args) const
	{
		// Make observers copy to allow parallel notifications
		celerix::unique_lock<celerix::mutex> lock{ mutex };
		auto observers_copy = observers;
		lock.unlock ();

		for (auto const & observer : observers_copy)
		{
			observer (args...);
		}
	}

	bool empty () const
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		return observers.empty ();
	}

	size_t size () const
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		return observers.size ();
	}

	celerix::container_info container_info () const
	{
		celerix::unique_lock<celerix::mutex> lock{ mutex };

		celerix::container_info info;
		info.put ("observers", observers);
		return info;
	}

private:
	mutable celerix::mutex mutex{ mutex_identifier (mutexes::observer_set) };
	std::vector<observer_type> observers;
};

}
