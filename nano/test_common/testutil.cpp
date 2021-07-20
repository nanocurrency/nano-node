#include <nano/crypto_lib/random_pool.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <numeric>

using namespace std::chrono_literals;

/* Convenience constants for tests which are always on the test network */
namespace
{
nano::ledger_constants dev_constants (nano::networks::nano_dev_network);
}

nano::keypair const & nano::dev_genesis_key (dev_constants.dev_genesis_key);
nano::account const & nano::nano_dev_account (dev_constants.nano_dev_account);
nano::uint128_t const & nano::genesis_amount (dev_constants.genesis_amount);
nano::account const & nano::burn_account (dev_constants.burn_account);

void nano::wait_peer_connections (nano::system & system_a)
{
	auto wait_peer_count = [&system_a] (bool in_memory) {
		auto num_nodes = system_a.nodes.size ();
		system_a.deadline_set (20s);
		size_t peer_count = 0;
		while (peer_count != num_nodes * (num_nodes - 1))
		{
			ASSERT_NO_ERROR (system_a.poll ());
			peer_count = std::accumulate (system_a.nodes.cbegin (), system_a.nodes.cend (), std::size_t{ 0 }, [in_memory] (auto total, auto const & node) {
				if (in_memory)
				{
					return total += node->network.size ();
				}
				else
				{
					auto transaction = node->store.tx_begin_read ();
					return total += node->store.peer.count (transaction);
				}
			});
		}
	};

	// Do a pre-pass with in-memory containers to reduce IO if still in the process of connecting to peers
	wait_peer_count (true);
	wait_peer_count (false);
}
