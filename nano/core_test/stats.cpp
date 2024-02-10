#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <ostream>

// Test stat counting at both type and detail levels
TEST (stats, stat_counting)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	node1.stats.add (nano::stat::type::ledger, nano::stat::dir::in, 1);
	node1.stats.add (nano::stat::type::ledger, nano::stat::dir::in, 5);
	node1.stats.inc (nano::stat::type::ledger, nano::stat::dir::in);
	node1.stats.inc (nano::stat::type::ledger, nano::stat::detail::send, nano::stat::dir::in);
	node1.stats.inc (nano::stat::type::ledger, nano::stat::detail::send, nano::stat::dir::in);
	node1.stats.inc (nano::stat::type::ledger, nano::stat::detail::receive, nano::stat::dir::in);
	ASSERT_EQ (10, node1.stats.count (nano::stat::type::ledger, nano::stat::dir::in));
	ASSERT_EQ (2, node1.stats.count (nano::stat::type::ledger, nano::stat::detail::send, nano::stat::dir::in));
	ASSERT_EQ (1, node1.stats.count (nano::stat::type::ledger, nano::stat::detail::receive, nano::stat::dir::in));
	node1.stats.add (nano::stat::type::ledger, nano::stat::dir::in, 0);
	ASSERT_EQ (10, node1.stats.count (nano::stat::type::ledger, nano::stat::dir::in));
}