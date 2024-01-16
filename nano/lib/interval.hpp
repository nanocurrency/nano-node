#pragma once

#include <chrono>

namespace nano
{
class interval
{
public:
	explicit interval (std::chrono::milliseconds target) :
		target{ target }
	{
	}

	bool elapsed ()
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
	std::chrono::milliseconds const target;
	std::chrono::steady_clock::time_point last{ std::chrono::steady_clock::now () };
};
}