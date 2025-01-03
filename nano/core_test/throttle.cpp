#include <nano/node/bootstrap/throttle.hpp>

#include <gtest/gtest.h>

TEST (throttle, construction)
{
	nano::bootstrap::throttle throttle{ 2 };
	ASSERT_FALSE (throttle.throttled ());
}

TEST (throttle, throttled)
{
	nano::bootstrap::throttle throttle{ 2 };
	throttle.add (false);
	ASSERT_FALSE (throttle.throttled ());
	throttle.add (false);
	ASSERT_TRUE (throttle.throttled ());
}

TEST (throttle, resize_up)
{
	nano::bootstrap::throttle throttle{ 2 };
	throttle.add (false);
	throttle.resize (4);
	ASSERT_FALSE (throttle.throttled ());
	throttle.add (false);
	ASSERT_TRUE (throttle.throttled ());
}

TEST (throttle, resize_down)
{
	nano::bootstrap::throttle throttle{ 4 };
	throttle.add (false);
	ASSERT_FALSE (throttle.throttled ());
	throttle.resize (2);
	ASSERT_FALSE (throttle.throttled ());
	throttle.add (false);
	ASSERT_TRUE (throttle.throttled ());
}
