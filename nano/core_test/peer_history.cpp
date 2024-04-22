#include <nano/node/peer_history.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (peer_history, store_live)
{
	nano::test::system system;

	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();
	auto & node3 = *system.add_node ();

	ASSERT_TIMELY (5s, node1.peer_history.exists (node2.network.endpoint ()));
	ASSERT_TIMELY (5s, node1.peer_history.exists (node3.network.endpoint ()));

	ASSERT_TIMELY (5s, node2.peer_history.exists (node1.network.endpoint ()));
	ASSERT_TIMELY (5s, node2.peer_history.exists (node3.network.endpoint ()));

	ASSERT_TIMELY (5s, node3.peer_history.exists (node1.network.endpoint ()));
	ASSERT_TIMELY (5s, node3.peer_history.exists (node2.network.endpoint ()));
}

TEST (peer_history, erase_old)
{
	nano::test::system system;

	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();

	ASSERT_TIMELY (5s, node1.peer_history.exists (node2.network.endpoint ()));
	ASSERT_TIMELY (5s, node2.peer_history.exists (node1.network.endpoint ()));

	// Endpoint won't be available after node is stopped
	auto node2_endpoint = node2.network.endpoint ();

	system.stop_node (node2);

	auto cached1 = node1.peer_history.peers ();
	ASSERT_EQ (cached1.size (), 1);
	ASSERT_EQ (cached1[0], node2_endpoint);

	ASSERT_TIMELY (5s, !node1.peer_history.exists (node2_endpoint));

	auto cached2 = node1.peer_history.peers ();
	ASSERT_EQ (cached2.size (), 0);
}