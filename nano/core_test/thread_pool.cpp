#include <nano/lib/thread_pool.hpp>
#include <nano/lib/timer.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <future>

TEST (thread_pool, single_task)
{
	std::atomic<bool> done (false);
	nano::mutex mutex;
	nano::condition_variable condition;
	nano::thread_pool workers (1u, nano::thread_role::name::unknown);
	nano::test::start_stop_guard stop_guard{ workers };
	workers.post ([&] () {
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			done = true;
		}
		condition.notify_one ();
	});
	nano::unique_lock<nano::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return !!done; });
}

TEST (thread_pool, many_tasks)
{
	std::atomic<int> count (0);
	nano::mutex mutex;
	nano::condition_variable condition;
	nano::thread_pool workers (8u, nano::thread_role::name::unknown);
	nano::test::start_stop_guard stop_guard{ workers };
	for (auto i (0); i < 100; ++i)
	{
		workers.post ([&] () {
			{
				nano::lock_guard<nano::mutex> lock{ mutex };
				count += 1;
			}
			condition.notify_one ();
		});
	}
	nano::unique_lock<nano::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return count == 100; });
}

TEST (thread_pool, tasks_counter)
{
	nano::thread_pool workers (1u, nano::thread_role::name::unknown);
	nano::test::start_stop_guard stop_guard{ workers };

	std::atomic<bool> hold_first_task{ true };

	workers.post ([&] () {
		while (hold_first_task)
		{
			std::this_thread::sleep_for (std::chrono::milliseconds (10));
		}
	});

	for (int i = 0; i < 5; ++i)
	{
		workers.post ([] () {
			std::this_thread::sleep_for (std::chrono::milliseconds (1));
		});
	}

	std::this_thread::sleep_for (std::chrono::milliseconds (50));

	ASSERT_EQ (6, workers.queued_tasks ());

	hold_first_task = false;

	// TODO: Use ASSERT_TIMELY when available
	nano::timer<std::chrono::milliseconds> timer;
	timer.start ();
	while (workers.queued_tasks () > 0 && timer.since_start () < std::chrono::seconds (5))
	{
		std::this_thread::sleep_for (std::chrono::milliseconds (50));
	}

	ASSERT_EQ (0, workers.queued_tasks ());
}

TEST (thread_pool, alive_status)
{
	nano::thread_pool workers (1u, nano::thread_role::name::unknown);
	ASSERT_FALSE (workers.alive ());

	{
		nano::test::start_stop_guard stop_guard{ workers };
		ASSERT_TRUE (workers.alive ());
	}

	ASSERT_FALSE (workers.alive ());
}

/*
 *
 */

TEST (timed_thread_pool, thread_pool)
{
	std::atomic<bool> passed_sleep{ false };

	auto func = [&passed_sleep] () {
		std::this_thread::sleep_for (std::chrono::seconds (1));
		passed_sleep = true;
	};

	nano::timed_thread_pool workers (1u, nano::thread_role::name::unknown);
	nano::test::start_stop_guard stop_guard{ workers };
	workers.post (func);
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

TEST (timed_thread_pool, one)
{
	std::atomic<bool> done (false);
	nano::mutex mutex;
	nano::condition_variable condition;
	nano::timed_thread_pool workers (1u, nano::thread_role::name::unknown);
	nano::test::start_stop_guard stop_guard{ workers };
	workers.post ([&] () {
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			done = true;
		}
		condition.notify_one ();
	});
	nano::unique_lock<nano::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return !!done; });
}

TEST (timed_thread_pool, many)
{
	std::atomic<int> count (0);
	nano::mutex mutex;
	nano::condition_variable condition;
	nano::timed_thread_pool workers (50u, nano::thread_role::name::unknown);
	nano::test::start_stop_guard stop_guard{ workers };
	for (auto i (0); i < 50; ++i)
	{
		workers.post ([&] () {
			{
				nano::lock_guard<nano::mutex> lock{ mutex };
				count += 1;
			}
			condition.notify_one ();
		});
	}
	nano::unique_lock<nano::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return count == 50; });
}

TEST (timed_thread_pool, delayed_execution)
{
	int value1 (0);
	int value2 (0);
	nano::mutex mutex;
	std::promise<bool> promise;
	nano::timed_thread_pool workers (1u, nano::thread_role::name::unknown);
	nano::test::start_stop_guard stop_guard{ workers };
	workers.post ([&] () {
		nano::lock_guard<nano::mutex> lock{ mutex };
		value1 = 1;
		value2 = 1;
	});
	workers.post_delayed (std::chrono::milliseconds (1), [&] () {
		nano::lock_guard<nano::mutex> lock{ mutex };
		value2 = 2;
		promise.set_value (false);
	});
	promise.get_future ().get ();
	nano::lock_guard<nano::mutex> lock{ mutex };
	ASSERT_EQ (1, value1);
	ASSERT_EQ (2, value2);
}