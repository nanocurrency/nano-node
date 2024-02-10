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

TEST (stats, stat_histogram)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);

	// Specific bins
	node1.stats.define_histogram (nano::stat::type::vote, nano::stat::detail::confirm_req, nano::stat::dir::in, { 1, 6, 10, 16 });
	node1.stats.update_histogram (nano::stat::type::vote, nano::stat::detail::confirm_req, nano::stat::dir::in, 1, 50);
	auto histogram_req (node1.stats.get_histogram (nano::stat::type::vote, nano::stat::detail::confirm_req, nano::stat::dir::in));
	ASSERT_EQ (histogram_req->get_bins ()[0].value, 50);

	// Uniform distribution (12 bins, width 1); also test clamping 100 to the last bin
	node1.stats.define_histogram (nano::stat::type::vote, nano::stat::detail::confirm_ack, nano::stat::dir::in, { 1, 13 }, 12);
	node1.stats.update_histogram (nano::stat::type::vote, nano::stat::detail::confirm_ack, nano::stat::dir::in, 1);
	node1.stats.update_histogram (nano::stat::type::vote, nano::stat::detail::confirm_ack, nano::stat::dir::in, 8, 10);
	node1.stats.update_histogram (nano::stat::type::vote, nano::stat::detail::confirm_ack, nano::stat::dir::in, 100);

	auto histogram_ack (node1.stats.get_histogram (nano::stat::type::vote, nano::stat::detail::confirm_ack, nano::stat::dir::in));
	ASSERT_EQ (histogram_ack->get_bins ()[0].value, 1);
	ASSERT_EQ (histogram_ack->get_bins ()[7].value, 10);
	ASSERT_EQ (histogram_ack->get_bins ()[11].value, 1);

	// Uniform distribution (2 bins, width 5); add 1 to each bin
	node1.stats.define_histogram (nano::stat::type::vote, nano::stat::detail::confirm_ack, nano::stat::dir::out, { 1, 11 }, 2);
	node1.stats.update_histogram (nano::stat::type::vote, nano::stat::detail::confirm_ack, nano::stat::dir::out, 1, 1);
	node1.stats.update_histogram (nano::stat::type::vote, nano::stat::detail::confirm_ack, nano::stat::dir::out, 6, 1);

	auto histogram_ack_out (node1.stats.get_histogram (nano::stat::type::vote, nano::stat::detail::confirm_ack, nano::stat::dir::out));
	ASSERT_EQ (histogram_ack_out->get_bins ()[0].value, 1);
	ASSERT_EQ (histogram_ack_out->get_bins ()[1].value, 1);
}