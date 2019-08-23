#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/utility.hpp>

#include <gtest/gtest.h>

namespace
{
std::atomic<bool> passed_sleep{ false };

void func ()
{
	std::this_thread::sleep_for (std::chrono::seconds (1));
	passed_sleep = true;
}
}

TEST (thread, worker)
{
	nano::worker worker;
	worker.push_task (func);
	ASSERT_FALSE (passed_sleep);

	nano::timer<std::chrono::milliseconds> timer_l;
	timer_l.start ();
	while (!passed_sleep)
	{
		if (timer_l.since_start () > std::chrono::seconds (10))
		{
			break;
		}
	}
	ASSERT_TRUE (passed_sleep);
}
