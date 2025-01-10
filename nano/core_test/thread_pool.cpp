#include <celerix/lib/thread_pool.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <future>

TEST (thread_pool, thread_pool)
{
	std::atomic<bool> passed_sleep{ false };

	auto func = [&passed_sleep] () {
		std::this_thread::sleep_for (std::chrono::seconds (1));
		passed_sleep = true;
	};

	celerix::thread_pool workers (1u, celerix::thread_role::name::unknown);
	celerix::test::start_stop_guard stop_guard{ workers };
	workers.post (func);
	ASSERT_FALSE (passed_sleep);

	celerix::timer<std::chrono::milliseconds> timer_l;
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

TEST (thread_pool, one)
{
	std::atomic<bool> done (false);
	celerix::mutex mutex;
	celerix::condition_variable condition;
	celerix::thread_pool workers (1u, celerix::thread_role::name::unknown);
	celerix::test::start_stop_guard stop_guard{ workers };
	workers.post ([&] () {
		{
			celerix::lock_guard<celerix::mutex> lock{ mutex };
			done = true;
		}
		condition.notify_one ();
	});
	celerix::unique_lock<celerix::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return !!done; });
}

TEST (thread_pool, many)
{
	std::atomic<int> count (0);
	celerix::mutex mutex;
	celerix::condition_variable condition;
	celerix::thread_pool workers (50u, celerix::thread_role::name::unknown);
	celerix::test::start_stop_guard stop_guard{ workers };
	for (auto i (0); i < 50; ++i)
	{
		workers.post ([&] () {
			{
				celerix::lock_guard<celerix::mutex> lock{ mutex };
				count += 1;
			}
			condition.notify_one ();
		});
	}
	celerix::unique_lock<celerix::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return count == 50; });
}

TEST (thread_pool, top_execution)
{
	int value1 (0);
	int value2 (0);
	celerix::mutex mutex;
	std::promise<bool> promise;
	celerix::thread_pool workers (1u, celerix::thread_role::name::unknown);
	celerix::test::start_stop_guard stop_guard{ workers };
	workers.post ([&] () {
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		value1 = 1;
		value2 = 1;
	});
	workers.post_delayed (std::chrono::milliseconds (1), [&] () {
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		value2 = 2;
		promise.set_value (false);
	});
	promise.get_future ().get ();
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	ASSERT_EQ (1, value1);
	ASSERT_EQ (2, value2);
}