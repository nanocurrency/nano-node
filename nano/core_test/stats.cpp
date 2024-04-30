#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <ostream>

// Test stat counting at both type and detail levels
TEST (stats, counters)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	node.stats.add (nano::stat::type::ledger, nano::stat::detail::test, nano::stat::dir::in, 1);
	node.stats.add (nano::stat::type::ledger, nano::stat::detail::test, nano::stat::dir::in, 5);
	node.stats.inc (nano::stat::type::ledger, nano::stat::detail::test, nano::stat::dir::in);
	node.stats.inc (nano::stat::type::ledger, nano::stat::detail::send, nano::stat::dir::in);
	node.stats.inc (nano::stat::type::ledger, nano::stat::detail::send, nano::stat::dir::in);
	node.stats.inc (nano::stat::type::ledger, nano::stat::detail::receive, nano::stat::dir::in);

	ASSERT_EQ (10, node.stats.count (nano::stat::type::ledger, nano::stat::dir::in));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::ledger, nano::stat::detail::send, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::ledger, nano::stat::detail::receive, nano::stat::dir::in));

	node.stats.add (nano::stat::type::ledger, nano::stat::detail::test, nano::stat::dir::in, 0);

	ASSERT_EQ (10, node.stats.count (nano::stat::type::ledger, nano::stat::dir::in));
}

TEST (stats, counters_aggregate_all)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	node.stats.add (nano::stat::type::ledger, nano::stat::detail::test, nano::stat::dir::in, 1, true);

	ASSERT_EQ (1, node.stats.count (nano::stat::type::ledger, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::ledger, nano::stat::detail::all, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::ledger, nano::stat::detail::test, nano::stat::dir::in));

	node.stats.add (nano::stat::type::ledger, nano::stat::detail::activate, nano::stat::dir::in, 5, true);

	ASSERT_EQ (6, node.stats.count (nano::stat::type::ledger, nano::stat::dir::in));
	ASSERT_EQ (6, node.stats.count (nano::stat::type::ledger, nano::stat::detail::all, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::ledger, nano::stat::detail::test, nano::stat::dir::in));
}

TEST (stats, samples)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	node.stats.sample (nano::stat::sample::active_election_duration, { 1, 10 }, 5);
	node.stats.sample (nano::stat::sample::active_election_duration, { 1, 10 }, 5);
	node.stats.sample (nano::stat::sample::active_election_duration, { 1, 10 }, 11);
	node.stats.sample (nano::stat::sample::active_election_duration, { 1, 10 }, 37);

	node.stats.sample (nano::stat::sample::bootstrap_tag_duration, { 1, 10 }, 2137);

	auto samples1 = node.stats.samples (nano::stat::sample::active_election_duration);
	ASSERT_EQ (4, samples1.size ());
	ASSERT_EQ (5, samples1[0]);
	ASSERT_EQ (5, samples1[1]);
	ASSERT_EQ (11, samples1[2]);
	ASSERT_EQ (37, samples1[3]);

	auto samples2 = node.stats.samples (nano::stat::sample::active_election_duration);
	ASSERT_EQ (0, samples2.size ());

	node.stats.sample (nano::stat::sample::active_election_duration, { 1, 10 }, 3);

	auto samples3 = node.stats.samples (nano::stat::sample::active_election_duration);
	ASSERT_EQ (1, samples3.size ());
	ASSERT_EQ (3, samples3[0]);

	auto samples4 = node.stats.samples (nano::stat::sample::bootstrap_tag_duration);
	ASSERT_EQ (1, samples4.size ());
	ASSERT_EQ (2137, samples4[0]);
}