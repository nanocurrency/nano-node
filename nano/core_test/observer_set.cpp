#include <nano/lib/observer_set.hpp>
#include <nano/lib/timer.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

using namespace std::chrono_literals;

TEST (observer_set, notify_one)
{
	nano::observer_set<int> set;
	int value{ 0 };
	set.add ([&value] (int v) {
		value = v;
	});
	set.notify (1);
	ASSERT_EQ (1, value);
}

TEST (observer_set, notify_multiple)
{
	nano::observer_set<int> set;
	int value{ 0 };
	set.add ([&value] (int v) {
		value = v;
	});
	set.add ([&value] (int v) {
		value += v;
	});
	set.notify (1);
	ASSERT_EQ (2, value);
}

TEST (observer_set, notify_empty)
{
	nano::observer_set<int> set;
	set.notify (1);
}

TEST (observer_set, notify_multiple_types)
{
	nano::observer_set<int, std::string> set;
	int value{ 0 };
	std::string str;
	set.add ([&value, &str] (int v, std::string s) {
		value = v;
		str = s;
	});
	set.notify (1, "test");
	ASSERT_EQ (1, value);
	ASSERT_EQ ("test", str);
}

TEST (observer_set, empty_params)
{
	nano::observer_set<> set;
	set.notify ();
}

// Make sure there are no TSAN warnings
TEST (observer_set, parallel_notify)
{
	nano::observer_set<int> set;
	std::atomic<int> value{ 0 };
	set.add ([&value] (int v) {
		std::this_thread::sleep_for (100ms);
		value = v;
	});
	nano::timer timer{ nano::timer_state::started };
	std::vector<std::thread> threads;
	for (int i = 0; i < 10; ++i)
	{
		threads.emplace_back ([&set] {
			set.notify (1);
		});
	}
	for (auto & thread : threads)
	{
		thread.join ();
	}
	ASSERT_EQ (1, value);
	// Notification should be done in parallel
	ASSERT_LT (timer.since_start (), 300ms);
}

namespace
{
struct move_only
{
	move_only () = default;
	move_only (move_only &&) = default;
	move_only & operator= (move_only &&) = default;
	move_only (move_only const &) = delete;
	move_only & operator= (move_only const &) = delete;
};

struct copy_throw
{
	copy_throw () = default;
	copy_throw (copy_throw &&) = default;
	copy_throw & operator= (copy_throw &&) = default;
	copy_throw (copy_throw const &)
	{
		throw std::runtime_error ("copy_throw");
	}
	copy_throw & operator= (copy_throw const &) = delete;
};
}

// Ensure that parameters are not unnecessarily copied, this should compile
TEST (observer_set, move_only)
{
	nano::observer_set<move_only> set;
	set.add ([] (move_only const &) {
	});
	move_only value;
	set.notify (value);
}

TEST (observer_set, copy_throw)
{
	nano::observer_set<copy_throw> set;
	set.add ([] (copy_throw const &) {
	});
	copy_throw value;
	ASSERT_NO_THROW (set.notify (value));
}

namespace
{
// Observer that counts how often it gets copied, moving it is free
struct copy_counting_observer
{
	explicit copy_counting_observer (int & copies) :
		copies{ &copies }
	{
	}
	copy_counting_observer (copy_counting_observer const & other) :
		copies{ other.copies }
	{
		++*copies;
	}
	copy_counting_observer (copy_counting_observer && other) noexcept :
		copies{ other.copies }
	{
	}
	void operator() (int const &) const
	{
	}
	int * copies;
};
}

/*
 * A notification calls the observers in place, only adding an observer may copy them.
 */
TEST (observer_set, notify_does_not_copy_observers)
{
	nano::observer_set<int> set;
	int copies{ 0 };
	set.add (copy_counting_observer{ copies });
	set.add (copy_counting_observer{ copies });
	auto const copies_after_add = copies;

	set.notify (1);
	set.notify (2);
	ASSERT_EQ (copies_after_add, copies);
}

/*
 * An observer added from inside a notification is first called by the next notification.
 */
TEST (observer_set, add_during_notify)
{
	nano::observer_set<int> set;
	int late_calls{ 0 };
	bool added{ false };
	set.add ([&] (int) {
		if (!added)
		{
			added = true;
			set.add ([&late_calls] (int) {
				++late_calls;
			});
		}
	});

	set.notify (1);
	ASSERT_EQ (0, late_calls);
	set.notify (1);
	ASSERT_EQ (1, late_calls);
	ASSERT_EQ (2, set.size ());
}
