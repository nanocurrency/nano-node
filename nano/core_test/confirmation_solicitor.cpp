#include <nano/lib/jsonconfig.hpp>
#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/testing.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (confirmation_solicitor, batches)
{
	nano::system system;
	nano::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_udp = false;
	auto & node1 = *system.add_node (node_flags);
	// This tests instantiates a solicitor
	node_flags.disable_request_loop = true;
	auto & node2 = *system.add_node (node_flags);
	// Solicitor will only solicit from this representative
	auto channel1 (node2.network.udp_channels.create (node1.network.endpoint ()));
	nano::representative representative (nano::test_genesis_key.pub, nano::genesis_amount, channel1);

	std::vector<nano::representative> representatives{ representative };
	nano::confirmation_solicitor solicitor (node2.network, node2.network_params.network);
	solicitor.prepare (representatives);
	// Ensure the representatives are correct
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (channel1, representatives.front ().channel);
	ASSERT_EQ (nano::test_genesis_key.pub, representatives.front ().account);
	auto send (std::make_shared<nano::send_block> (nano::genesis_hash, nano::keypair ().pub, nano::genesis_amount - 100, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *system.work.generate (nano::genesis_hash)));
	{
		nano::lock_guard<std::mutex> guard (node2.active.mutex);
		for (size_t i (0); i < nano::network::confirm_req_hashes_max; ++i)
		{
			auto election (std::make_shared<nano::election> (node2, send, nullptr, false));
			ASSERT_FALSE (solicitor.add (*election));
		}
		ASSERT_EQ (1, solicitor.max_confirm_req_batches);
		// Reached the maximum amount of requests for the channel
		auto election (std::make_shared<nano::election> (node2, send, nullptr, false));
		ASSERT_TRUE (solicitor.add (*election));
		// Broadcasting should be immediate
		ASSERT_EQ (0, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out));
		ASSERT_FALSE (solicitor.broadcast (*election));
	}
	system.deadline_set (5s);
	while (node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out) < 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// From rep crawler
	system.deadline_set (5s);
	solicitor.flush ();
	while (node2.stats.count (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out) == 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_LE (2, node2.stats.count (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out));
}
