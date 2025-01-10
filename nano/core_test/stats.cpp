#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <ostream>

// Test stat counting at both type and detail levels
TEST (stats, counters)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	node.stats.add (celerix::stat::type::ledger, celerix::stat::detail::test, celerix::stat::dir::in, 1);
	node.stats.add (celerix::stat::type::ledger, celerix::stat::detail::test, celerix::stat::dir::in, 5);
	node.stats.inc (celerix::stat::type::ledger, celerix::stat::detail::test, celerix::stat::dir::in);
	node.stats.inc (celerix::stat::type::ledger, celerix::stat::detail::send, celerix::stat::dir::in);
	node.stats.inc (celerix::stat::type::ledger, celerix::stat::detail::send, celerix::stat::dir::in);
	node.stats.inc (celerix::stat::type::ledger, celerix::stat::detail::receive, celerix::stat::dir::in);

	ASSERT_EQ (10, node.stats.count (celerix::stat::type::ledger, celerix::stat::dir::in));
	ASSERT_EQ (2, node.stats.count (celerix::stat::type::ledger, celerix::stat::detail::send, celerix::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::ledger, celerix::stat::detail::receive, celerix::stat::dir::in));

	node.stats.add (celerix::stat::type::ledger, celerix::stat::detail::test, celerix::stat::dir::in, 0);

	ASSERT_EQ (10, node.stats.count (celerix::stat::type::ledger, celerix::stat::dir::in));
}

TEST (stats, counters_aggregate_all)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	node.stats.add (celerix::stat::type::ledger, celerix::stat::detail::test, celerix::stat::dir::in, 1, true);

	ASSERT_EQ (1, node.stats.count (celerix::stat::type::ledger, celerix::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::ledger, celerix::stat::detail::all, celerix::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::ledger, celerix::stat::detail::test, celerix::stat::dir::in));

	node.stats.add (celerix::stat::type::ledger, celerix::stat::detail::activate, celerix::stat::dir::in, 5, true);

	ASSERT_EQ (6, node.stats.count (celerix::stat::type::ledger, celerix::stat::dir::in));
	ASSERT_EQ (6, node.stats.count (celerix::stat::type::ledger, celerix::stat::detail::all, celerix::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::ledger, celerix::stat::detail::test, celerix::stat::dir::in));
}

TEST (stats, samples)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	node.stats.sample (celerix::stat::sample::active_election_duration, 5, { 1, 10 });
	node.stats.sample (celerix::stat::sample::active_election_duration, 5, { 1, 10 });
	node.stats.sample (celerix::stat::sample::active_election_duration, 11, { 1, 10 });
	node.stats.sample (celerix::stat::sample::active_election_duration, 37, { 1, 10 });

	node.stats.sample (celerix::stat::sample::bootstrap_tag_duration, 2137, { 1, 10 });

	auto samples1 = node.stats.samples (celerix::stat::sample::active_election_duration);
	ASSERT_EQ (4, samples1.size ());
	ASSERT_EQ (5, samples1[0]);
	ASSERT_EQ (5, samples1[1]);
	ASSERT_EQ (11, samples1[2]);
	ASSERT_EQ (37, samples1[3]);

	auto samples2 = node.stats.samples (celerix::stat::sample::active_election_duration);
	ASSERT_EQ (0, samples2.size ());

	node.stats.sample (celerix::stat::sample::active_election_duration, 3, { 1, 10 });

	auto samples3 = node.stats.samples (celerix::stat::sample::active_election_duration);
	ASSERT_EQ (1, samples3.size ());
	ASSERT_EQ (3, samples3[0]);

	auto samples4 = node.stats.samples (celerix::stat::sample::bootstrap_tag_duration);
	ASSERT_EQ (1, samples4.size ());
	ASSERT_EQ (2137, samples4[0]);
}