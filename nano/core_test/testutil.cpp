#if ACTIVE_NETWORK == nano_test_network
#include <iostream>
#include <nano/lib/utility.hpp>
#include <gtest/gtest.h>

/*
 * If we are running under the test harness, mark this test as failed
 * with additional information so that we can gather information
 * about further tests
 */

void release_assert_internal (bool check, const char * check_expr, const char * file, unsigned int line)
{
	if (check)
	{
		return;
	}

	ASSERT_TRUE (false) << "Assertion (" << check_expr << ") failed at " << file << ":" << line;
}
#endif
