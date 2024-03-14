#include <nano/lib/timer.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

/* Tests for the timer utility. Note that we use sleep_for in the tests, which
 sleeps for *at least* the given amount. We thus allow for some leeway in the
 upper bound checks (also because CI is often very slow) */

TEST (timer, states)
{
	nano::timer<std::chrono::milliseconds> t1;
	ASSERT_EQ (t1.current_state (), nano::timer_state::stopped);
	t1.start ();
	ASSERT_EQ (t1.current_state (), nano::timer_state::started);
	t1.restart ();
	ASSERT_EQ (t1.current_state (), nano::timer_state::started);
	t1.pause ();
	ASSERT_EQ (t1.current_state (), nano::timer_state::stopped);
	t1.start ();
	ASSERT_EQ (t1.current_state (), nano::timer_state::started);
	t1.stop ();
	ASSERT_EQ (t1.current_state (), nano::timer_state::stopped);

	nano::timer<std::chrono::milliseconds> t2 (nano::timer_state::started);
	ASSERT_EQ (t2.current_state (), nano::timer_state::started);
	t2.stop ();
	ASSERT_EQ (t2.current_state (), nano::timer_state::stopped);
}

TEST (timer, measure_and_compare)
{
	using namespace std::chrono_literals;
	nano::timer<std::chrono::milliseconds> t1 (nano::timer_state::started);
	ASSERT_EQ (t1.current_state (), nano::timer_state::started);
	std::this_thread::sleep_for (50ms);
	ASSERT_TRUE (t1.after_deadline (30ms));
	ASSERT_TRUE (t1.before_deadline (500ms));
	ASSERT_LT (t1.since_start (), 500ms);
	ASSERT_GT (t1.since_start (), 10ms);
	ASSERT_GE (t1.stop (), 50ms);
	std::this_thread::sleep_for (50ms);
	ASSERT_GT (t1.restart (), 10ms);
}

TEST (timer, cummulative_child)
{
	using namespace std::chrono_literals;
	nano::timer<std::chrono::milliseconds> t1 (nano::timer_state::started);
	auto & child1 = t1.child ();
	for (int i = 0; i < 10; i++)
	{
		child1.start ();
		std::this_thread::sleep_for (5ms);
		child1.pause ();
	}
	ASSERT_GE (child1.value (), 50ms);
	ASSERT_LT (child1.value (), 500ms);

	auto & child2 = t1.child ();
	for (int i = 0; i < 10; i++)
	{
		child2.start ();
		std::this_thread::sleep_for (5ms);
		child2.pause ();
	}
	ASSERT_GE (child2.value (), 50ms);
	ASSERT_LT (child2.value (), 500ms);

	ASSERT_GT (t1.stop (), 100ms);
}

TEST (timer, stop)
{
	using namespace std::chrono_literals;
	nano::timer<std::chrono::milliseconds> t1 (nano::timer_state::started);
	std::this_thread::sleep_for (50ms);
	auto stop_value = t1.stop ();
	std::this_thread::sleep_for (50ms);
	ASSERT_EQ (t1.value (), stop_value);
}
