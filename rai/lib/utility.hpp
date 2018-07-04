#pragma once

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace rai
{
// Lower priority of calling work generating thread
void work_thread_reprioritize ();
template <typename... T>
class observer_set
{
public:
	void add (std::function<void(T...)> const & observer_a)
	{
		std::lock_guard<std::mutex> lock (mutex);
		observers.push_back (observer_a);
	}
	void notify (T... args)
	{
		std::lock_guard<std::mutex> lock (mutex);
		for (auto & i : observers)
		{
			i (args...);
		}
	}
	std::mutex mutex;
	std::vector<std::function<void(T...)>> observers;
};
}
