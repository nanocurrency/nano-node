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

void nano::compare_default_telemetry_response_data_excluding_signature (nano::telemetry_data const & telemetry_data_a, nano::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a)
{
	ASSERT_EQ (telemetry_data_a.block_count, 1);
	ASSERT_EQ (telemetry_data_a.cemented_count, 1);
	ASSERT_EQ (telemetry_data_a.bandwidth_cap, bandwidth_limit_a);
	ASSERT_EQ (telemetry_data_a.peer_count, 1);
	ASSERT_EQ (telemetry_data_a.protocol_version, network_params_a.protocol.telemetry_protocol_version_min);
	ASSERT_EQ (telemetry_data_a.unchecked_count, 0);
	ASSERT_EQ (telemetry_data_a.account_count, 1);
	ASSERT_LT (telemetry_data_a.uptime, 100);
	ASSERT_EQ (telemetry_data_a.genesis_block, network_params_a.ledger.genesis_hash);
	ASSERT_EQ (telemetry_data_a.major_version, nano::get_major_node_version ());
	ASSERT_EQ (telemetry_data_a.minor_version, nano::get_minor_node_version ());
	ASSERT_EQ (telemetry_data_a.patch_version, nano::get_patch_node_version ());
	ASSERT_EQ (telemetry_data_a.pre_release_version, nano::get_pre_release_node_version ());
	ASSERT_EQ (telemetry_data_a.maker, 0);
	ASSERT_GT (telemetry_data_a.timestamp, std::chrono::system_clock::now () - std::chrono::seconds (100));
	ASSERT_EQ (telemetry_data_a.active_difficulty, active_difficulty_a);
}

void nano::compare_default_telemetry_response_data (nano::telemetry_data const & telemetry_data_a, nano::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a, nano::keypair const & node_id_a)
{
	ASSERT_FALSE (telemetry_data_a.validate_signature (nano::telemetry_data::size));
	nano::telemetry_data telemetry_data_l = telemetry_data_a;
	telemetry_data_l.signature.clear ();
	telemetry_data_l.sign (node_id_a);
	// Signature should be different because uptime/timestamp will have changed.
	ASSERT_NE (telemetry_data_a.signature, telemetry_data_l.signature);
	compare_default_telemetry_response_data_excluding_signature (telemetry_data_a, network_params_a, bandwidth_limit_a, active_difficulty_a);
	ASSERT_EQ (telemetry_data_a.node_id, node_id_a.pub);
}
