#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/testing.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <numeric>

using namespace std::chrono_literals;

/* Convenience constants for tests which are always on the test network */
namespace
{
nano::ledger_constants test_constants (nano::nano_networks::nano_test_network);
}

nano::keypair const & nano::zero_key (test_constants.zero_key);
nano::keypair const & nano::test_genesis_key (test_constants.test_genesis_key);
nano::account const & nano::nano_test_account (test_constants.nano_test_account);
std::string const & nano::nano_test_genesis (test_constants.nano_test_genesis);
nano::account const & nano::genesis_account (test_constants.genesis_account);
nano::block_hash const & nano::genesis_hash (test_constants.genesis_hash);
nano::uint128_t const & nano::genesis_amount (test_constants.genesis_amount);
nano::account const & nano::burn_account (test_constants.burn_account);

void nano::wait_peer_connections (nano::system & system_a)
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
