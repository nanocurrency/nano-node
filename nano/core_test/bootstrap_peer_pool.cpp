#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/peer_pool.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/test_common/system.hpp>

#include <gtest/gtest.h>

#include <array>
#include <optional>

namespace
{
class bootstrap_peer_pool : public ::testing::Test
{
protected:
	std::shared_ptr<nano::transport::channel> make_channel (nano::account node_id, nano::node_capabilities_flags capabilities = {}, std::optional<uint8_t> protocol_version = std::nullopt)
	{
		auto & node = system.node (0);
		nano::transport::peer_info const peer{
			.node_id = node_id,
			.protocol_version = protocol_version.value_or (node.network_params.network.protocol_version),
			.capabilities = capabilities,
		};
		return std::make_shared<nano::transport::fake::channel> (node, peer);
	}

	nano::test::system system{ 1 };
	nano::bootstrap_config config;
	nano::bootstrap::peer_pool pool{ config };
};
}

TEST_F (bootstrap_peer_pool, update_reset)
{
	ASSERT_EQ (0, pool.size ());
	ASSERT_EQ (0, pool.available ());

	auto channel1 = make_channel ({ 1 });
	auto channel2 = make_channel ({ 2 });
	pool.update ({ channel1, channel2 });
	ASSERT_EQ (2, pool.size ());
	ASSERT_EQ (2, pool.available ());

	pool.update ({ channel1, channel2 });
	ASSERT_EQ (2, pool.size ());

	channel1->close ();
	pool.update ({ channel1, channel2 });
	ASSERT_EQ (1, pool.size ());

	pool.reset ();
	ASSERT_EQ (0, pool.size ());
}

TEST_F (bootstrap_peer_pool, acquire_balances_load_and_enforces_capacity)
{
	config.channel_limit = 2;
	auto channel1 = make_channel ({ 1 });
	auto channel2 = make_channel ({ 2 });
	pool.update ({ channel1, channel2 });

	auto first = pool.acquire ();
	auto second = pool.acquire ();
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::acquired, first.status);
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::acquired, second.status);
	ASSERT_NE (first.channel, second.channel);
	ASSERT_EQ (2, pool.available ());

	ASSERT_EQ (nano::bootstrap::peer_acquire_status::acquired, pool.acquire ().status);
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::acquired, pool.acquire ().status);
	ASSERT_EQ (0, pool.available ());

	auto busy = pool.acquire ();
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::busy, busy.status);
	ASSERT_EQ (nullptr, busy.channel);

	pool.release (first.channel);
	ASSERT_EQ (1, pool.available ());
	auto released = pool.acquire ();
	ASSERT_EQ (first.channel, released.channel);
}

TEST_F (bootstrap_peer_pool, capabilities_and_exclusions)
{
	auto topo_channel = make_channel ({ 1 }, nano::node_capabilities::topo_index);
	auto vote_channel = make_channel ({ 2 }, nano::node_capabilities::vote_storage);
	pool.update ({ topo_channel, vote_channel });

	nano::bootstrap::peer_requirements topo_requirements{ nano::node_capabilities::topo_index };
	auto topo = pool.acquire (topo_requirements);
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::acquired, topo.status);
	ASSERT_EQ (topo_channel, topo.channel);
	ASSERT_EQ (nano::account{ 1 }, topo.node_id);

	nano::bootstrap::peer_requirements required;
	required.capabilities = nano::node_capabilities_flags{ nano::node_capabilities::topo_index } | nano::node_capabilities::vote_storage;
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::no_peers, pool.acquire (required).status);
	ASSERT_FALSE (pool.has_candidate (required));

	std::array excluded{ nano::account{ 1 } };
	auto exhausted = pool.acquire (topo_requirements, excluded);
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::exhausted, exhausted.status);
	ASSERT_EQ (nullptr, exhausted.channel);
	ASSERT_FALSE (pool.has_candidate (topo_requirements, excluded));
}

TEST_F (bootstrap_peer_pool, minimum_protocol_version)
{
	auto const minimum_version = system.node (0).network_params.network.topo_bootstrap_protocol_version_min;
	auto older_channel = make_channel ({ 1 }, {}, minimum_version - 1);
	auto compatible_channel = make_channel ({ 2 });
	pool.update ({ older_channel, compatible_channel });

	nano::bootstrap::peer_requirements requirements;
	requirements.minimum_protocol_version = minimum_version;
	auto result = pool.acquire (requirements);
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::acquired, result.status);
	ASSERT_EQ (compatible_channel, result.channel);
	ASSERT_TRUE (pool.has_candidate (requirements));
}

TEST_F (bootstrap_peer_pool, probe_release_and_decay)
{
	config.channel_limit = 1;
	auto channel = make_channel ({ 1 });
	pool.update ({ channel });

	ASSERT_EQ (nano::bootstrap::peer_probe_status::available, pool.probe ());
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::acquired, pool.acquire ().status);
	ASSERT_EQ (nano::bootstrap::peer_probe_status::busy, pool.probe ());

	pool.release (channel);
	ASSERT_EQ (nano::bootstrap::peer_probe_status::available, pool.probe ());
	ASSERT_EQ (nano::bootstrap::peer_acquire_status::acquired, pool.acquire ().status);
	pool.decay ();
	ASSERT_EQ (nano::bootstrap::peer_probe_status::available, pool.probe ());
}
