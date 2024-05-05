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
	void add (std::function<void (T...)> const & observer_a)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		observers.push_back (observer_a);
	}

	void notify (T... args) const
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		auto observers_copy = observers;
		lock.unlock ();

		for (auto & i : observers_copy)
		{
			i (args...);
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
	std::vector<std::function<void (T...)>> observers;
};

}
