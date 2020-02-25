#include <nano/core_test/testutil.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (confirmation_solicitor, batches)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	nano::node_flags node_flags;
	node_flags.disable_udp = false;
	auto & node1 = *system.add_node (node_config, node_flags);
	node_config.peering_port = nano::get_available_port ();
	// To prevent races on the solicitor
	node_flags.disable_request_loop = true;
	auto & node2 = *system.add_node (node_config, node_flags);
	// Solicitor will only solicit from this representative
	auto channel1 (node2.network.udp_channels.create (node1.network.endpoint ()));
	nano::representative representative (nano::test_genesis_key.pub, nano::genesis_amount, channel1);
	// Lock active_transactions which uses the solicitor
	{
		nano::lock_guard<std::mutex> active_guard (node2.active.mutex);
		std::vector<nano::representative> representatives{ representative };
		node2.active.solicitor.prepare (representatives);
		// Ensure the representatives are correct
		ASSERT_EQ (1, representatives.size ());
		ASSERT_EQ (channel1, representatives.front ().channel);
		ASSERT_EQ (nano::test_genesis_key.pub, representatives.front ().account);
		auto send (std::make_shared<nano::send_block> (nano::genesis_hash, nano::keypair ().pub, nano::genesis_amount - 100, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (nano::genesis_hash)));
		for (size_t i (0); i < nano::network::confirm_req_hashes_max; ++i)
		{
			auto election (std::make_shared<nano::election> (node2, send, false, nullptr));
			ASSERT_FALSE (node2.active.solicitor.add (*election));
		}
		ASSERT_EQ (1, node2.active.solicitor.max_confirm_req_batches);
		// Reached the maximum amount of requests for the channel
		auto election (std::make_shared<nano::election> (node2, send, false, nullptr));
		ASSERT_TRUE (node2.active.solicitor.add (*election));
		// Broadcasting should be immediate
		ASSERT_EQ (0, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out));
		ASSERT_FALSE (node2.active.solicitor.broadcast (*election));
		system.deadline_set (5s);
		while (node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out) < 1)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}
	// From rep crawler
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out));
	system.deadline_set (5s);
	node2.active.solicitor.flush ();
	while (node2.stats.count (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out) < 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}
