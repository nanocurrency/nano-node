#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rai
{
/** Read entire file into byte vector */
std::vector<char> read_file (std::string path, bool & error);
/** Returns the hex representation of the SHA256 hash over the input bytes */
std::string sha256 (std::vector<char>);
/** Returns the hex representation of the SHA256 hash over the input bytes */
std::string sha256 (const unsigned char * bytes, size_t len);

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
