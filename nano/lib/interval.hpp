#pragma once

#include <chrono>
#include <mutex>

namespace nano
{
class interval
{
public:
	bool elapse (auto target)
	{
		auto const now = std::chrono::steady_clock::now ();
		if (now - last >= target)
		{
			last = now;
			return true;
		}
		return false;
	}

private:
	std::chrono::steady_clock::time_point last{};
};

class interval_mt
{
public:
	bool elapse (auto target)
	{
		std::lock_guard guard{ mutex };
		auto const now = std::chrono::steady_clock::now ();
		if (now - last >= target)
		{
			last = now;
			return true;
		}
		return false;
	}

private:
	std::mutex mutex;
	std::chrono::steady_clock::time_point last{};
};
}