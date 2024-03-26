#pragma once

#include <chrono>

namespace nano
{
class interval
{
public:
	bool elapsed (auto target)
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
	std::chrono::steady_clock::time_point last{ std::chrono::steady_clock::now () };
};
}