#pragma once

#include <functional>
#include <vector>

#include <nano/lib/locks.hpp>
#include <nano/lib/utility.hpp>

namespace nano
{

template <typename... T>
class observer_set final
{
public:
	void add (std::function<void (T...)> const & observer_a)
	{
		nano::lock_guard<nano::mutex> lock (mutex);
		observers.push_back (observer_a);
	}

	void notify (T... args) const
	{
		nano::unique_lock<nano::mutex> lock (mutex);
		auto observers_copy = observers;
		lock.unlock ();

		for (auto & i : observers_copy)
		{
			i (args...);
		}
	}

	bool empty () const
	{
		nano::lock_guard<nano::mutex> lock (mutex);
		return observers.empty();
	}

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const
	{
		nano::unique_lock<nano::mutex> lock (mutex);
		auto count = observers.size ();
		lock.unlock();
		auto sizeof_element = sizeof (typename decltype (observers)::value_type);
		auto composite = std::make_unique<container_info_composite> (name);
		composite->add_component (std::make_unique<container_info_leaf> (container_info{ "observers", count, sizeof_element }));
		return composite;
	}

private:
	mutable nano::mutex mutex{ mutex_identifier (mutexes::observer_set) };
	std::vector<std::function<void (T...)>> observers;
};

}
