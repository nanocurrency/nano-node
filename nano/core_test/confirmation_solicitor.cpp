#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/node/block_rebroadcaster.hpp>
#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace
{
std::shared_ptr<nano::block> test_block (nano::test::system & system)
{
	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (nano::dev::genesis->hash ())
				.destination (nano::keypair ().pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	send->sideband_set ({});
	return send;
}

nano::election_snapshot test_snapshot (std::shared_ptr<nano::block> const & winner, std::unordered_map<nano::account, nano::vote_info> votes = {}, bool quorum = false)
{
	return { winner->qualified_root (), winner, quorum, std::move (votes) };
}
}

TEST (confirmation_solicitor, batches)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	nano::node_config config1 = system.default_config ();
	config1.block_rebroadcaster->enable = false;
	auto & node1 = *system.add_node (config1, node_flags);
	nano::node_config config2 = system.default_config ();
	config2.block_rebroadcaster->enable = false;
	auto & node2 = *system.add_node (config2, node_flags);
	auto channel1 = nano::test::establish_tcp (system, node2, node1.network.endpoint ());
	// Solicitor will only solicit from this representative
	nano::representative representative{ nano::dev::genesis_key.pub, channel1 };
	std::vector<nano::representative> representatives{ representative };
	nano::confirmation_solicitor solicitor (node2.network, node2.config);
	solicitor.prepare (representatives);
	// Ensure the representatives are correct
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (channel1, representatives.front ().channel);
	ASSERT_EQ (nano::dev::genesis_key.pub, representatives.front ().account);
	ASSERT_TIMELY_EQ (3s, node2.network.size (), 1);
	auto send = test_block (system);
	for (size_t i (0); i < nano::network::confirm_req_hashes_max; ++i)
	{
		ASSERT_TRUE (solicitor.add (test_snapshot (send)));
	}
	// Broadcasting should be immediate
	ASSERT_EQ (0, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out));
	ASSERT_TRUE (solicitor.broadcast (test_snapshot (send)));
	// One publish through directed broadcasting (random flooding moved to block_rebroadcaster)
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out));
	solicitor.flush ();
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out));
}

TEST (confirmation_solicitor, different_hash)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	nano::node_config config1 = system.default_config ();
	config1.block_rebroadcaster->enable = false;
	auto & node1 = *system.add_node (config1, node_flags);
	nano::node_config config2 = system.default_config ();
	config2.block_rebroadcaster->enable = false;
	auto & node2 = *system.add_node (config2, node_flags);
	auto channel1 = nano::test::establish_tcp (system, node2, node1.network.endpoint ());
	// Solicitor will only solicit from this representative
	nano::representative representative{ nano::dev::genesis_key.pub, channel1 };
	std::vector<nano::representative> representatives{ representative };
	nano::confirmation_solicitor solicitor (node2.network, node2.config);
	solicitor.prepare (representatives);
	// Ensure the representatives are correct
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (channel1, representatives.front ().channel);
	ASSERT_EQ (nano::dev::genesis_key.pub, representatives.front ().account);
	ASSERT_TIMELY_EQ (3s, node2.network.size (), 1);
	auto send = test_block (system);
	// The representative voted for something else, not the winner
	std::unordered_map<nano::account, nano::vote_info> votes;
	votes[representative.account] = { std::chrono::steady_clock::now (), 1, 1 };
	auto snapshot = test_snapshot (send, votes);
	// Ensure the request and broadcast goes through
	ASSERT_TRUE (solicitor.add (snapshot));
	ASSERT_TRUE (solicitor.broadcast (snapshot));
	// One publish through directed broadcasting (random flooding moved to block_rebroadcaster)
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out));
	solicitor.flush ();
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out));
}

TEST (confirmation_solicitor, bypass_max_requests_cap)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);
	nano::confirmation_solicitor solicitor (node2.network, node2.config);
	std::vector<nano::representative> representatives;
	auto max_representatives = std::max<size_t> (solicitor.max_election_requests, solicitor.max_election_broadcasts);
	representatives.reserve (max_representatives + 1);
	for (auto i (0); i < max_representatives + 1; ++i)
	{
		// Make a temporary channel associated with node2
		auto channel = std::make_shared<nano::transport::inproc::channel> (node2, node2);
		nano::representative representative{ nano::account (i), channel };
		representatives.push_back (representative);
	}
	ASSERT_EQ (max_representatives + 1, representatives.size ());
	solicitor.prepare (representatives);
	ASSERT_TIMELY_EQ (3s, node2.network.size (), 1);
	auto send = test_block (system);
	// Every representative voted for something else, not the winner
	std::unordered_map<nano::account, nano::vote_info> votes;
	for (auto const & rep : representatives)
	{
		votes[rep.account] = { std::chrono::steady_clock::now (), 1, 1 };
	}
	ASSERT_TRUE (solicitor.add (test_snapshot (send, votes)));
	ASSERT_TRUE (solicitor.broadcast (test_snapshot (send, votes)));
	solicitor.flush ();
	// All requests went through, the last one would normally not go through due to the cap but a vote for a different hash does not count towards the cap
	ASSERT_TIMELY_EQ (6s, max_representatives + 1, node2.stats.count (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out));

	solicitor.prepare (representatives);
	ASSERT_TRUE (solicitor.add (test_snapshot (send)));
	ASSERT_TRUE (solicitor.broadcast (test_snapshot (send)));

	solicitor.flush ();

	// All requests but one went through, due to the cap
	ASSERT_EQ (2 * max_representatives + 1, node2.stats.count (nano::stat::type::message, nano::stat::detail::confirm_req, nano::stat::dir::out));
}
