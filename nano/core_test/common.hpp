#pragma once

#include <nano/core_test/testutil.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

namespace nano
{
inline void wait_peer_connections (nano::system & system_a)
{
	auto wait_peer_count = [&system_a](bool in_memory) {
		auto num_nodes = system_a.nodes.size ();
		system_a.deadline_set (20s);
		size_t peer_count = 0;
		while (peer_count != num_nodes * (num_nodes - 1))
		{
			ASSERT_NO_ERROR (system_a.poll ());
			peer_count = std::accumulate (system_a.nodes.cbegin (), system_a.nodes.cend (), std::size_t{ 0 }, [in_memory](auto total, auto const & node) {
				if (in_memory)
				{
					return total += node->network.size ();
				}
				else
				{
					auto transaction = node->store.tx_begin_read ();
					return total += node->store.peer_count (transaction);
				}
			});
		}
	};

	// Do a pre-pass with in-memory containers to reduce IO if still in the process of connecting to peers
	wait_peer_count (true);
	wait_peer_count (false);
}
}
