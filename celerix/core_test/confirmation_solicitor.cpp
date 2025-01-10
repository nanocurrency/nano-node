#include <celerix/lib/blocks.hpp>
#include <celerix/lib/jsonconfig.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/confirmation_solicitor.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/transport/inproc.hpp>
#include <celerix/test_common/network.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (confirmation_solicitor, batches)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (node_flags);
	node_flags.disable_request_loop = true;
	auto & node2 = *system.add_node (node_flags);
	auto channel1 = celerix::test::establish_tcp (system, node2, node1.network.endpoint ());
	// Solicitor will only solicit from this representative
	celerix::representative representative{ celerix::dev::genesis_key.pub, channel1 };
	std::vector<celerix::representative> representatives{ representative };
	celerix::confirmation_solicitor solicitor (node2.network, node2.config);
	solicitor.prepare (representatives);
	// Ensure the representatives are correct
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (channel1, representatives.front ().channel);
	ASSERT_EQ (celerix::dev::genesis_key.pub, representatives.front ().account);
	ASSERT_TIMELY_EQ (3s, node2.network.size (), 1);
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (celerix::dev::genesis->hash ())
				.destination (celerix::keypair ().pub)
				.balance (celerix::dev::constants.genesis_amount - 100)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (celerix::dev::genesis->hash ()))
				.build ();
	send->sideband_set ({});
	{
		celerix::lock_guard<celerix::mutex> guard (node2.active.mutex);
		for (size_t i (0); i < celerix::network::confirm_req_hashes_max; ++i)
		{
			auto election (std::make_shared<celerix::election> (node2, send, nullptr, nullptr, celerix::election_behavior::priority));
			ASSERT_FALSE (solicitor.add (*election));
		}
		// Reached the maximum amount of requests for the channel
		auto election (std::make_shared<celerix::election> (node2, send, nullptr, nullptr, celerix::election_behavior::priority));
		// Broadcasting should be immediate
		ASSERT_EQ (0, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::out));
		ASSERT_FALSE (solicitor.broadcast (*election));
	}
	// One publish through directed broadcasting and another through random flooding
	ASSERT_EQ (2, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::out));
	solicitor.flush ();
	ASSERT_EQ (1, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_req, celerix::stat::dir::out));
}

namespace celerix
{
TEST (confirmation_solicitor, different_hash)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);
	auto channel1 = celerix::test::establish_tcp (system, node2, node1.network.endpoint ());
	// Solicitor will only solicit from this representative
	celerix::representative representative{ celerix::dev::genesis_key.pub, channel1 };
	std::vector<celerix::representative> representatives{ representative };
	celerix::confirmation_solicitor solicitor (node2.network, node2.config);
	solicitor.prepare (representatives);
	// Ensure the representatives are correct
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (channel1, representatives.front ().channel);
	ASSERT_EQ (celerix::dev::genesis_key.pub, representatives.front ().account);
	ASSERT_TIMELY_EQ (3s, node2.network.size (), 1);
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (celerix::dev::genesis->hash ())
				.destination (celerix::keypair ().pub)
				.balance (celerix::dev::constants.genesis_amount - 100)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (celerix::dev::genesis->hash ()))
				.build ();
	send->sideband_set ({});
	auto election (std::make_shared<celerix::election> (node2, send, nullptr, nullptr, celerix::election_behavior::priority));
	// Add a vote for something else, not the winner
	election->last_votes[representative.account] = { std::chrono::steady_clock::now (), 1, 1 };
	// Ensure the request and broadcast goes through
	ASSERT_FALSE (solicitor.add (*election));
	ASSERT_FALSE (solicitor.broadcast (*election));
	// One publish through directed broadcasting and another through random flooding
	ASSERT_EQ (2, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::publish, celerix::stat::dir::out));
	solicitor.flush ();
	ASSERT_EQ (1, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_req, celerix::stat::dir::out));
}

TEST (confirmation_solicitor, bypass_max_requests_cap)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);
	celerix::confirmation_solicitor solicitor (node2.network, node2.config);
	std::vector<celerix::representative> representatives;
	auto max_representatives = std::max<size_t> (solicitor.max_election_requests, solicitor.max_election_broadcasts);
	representatives.reserve (max_representatives + 1);
	for (auto i (0); i < max_representatives + 1; ++i)
	{
		// Make a temporary channel associated with node2
		auto channel = std::make_shared<celerix::transport::inproc::channel> (node2, node2);
		celerix::representative representative{ celerix::account (i), channel };
		representatives.push_back (representative);
	}
	ASSERT_EQ (max_representatives + 1, representatives.size ());
	solicitor.prepare (representatives);
	ASSERT_TIMELY_EQ (3s, node2.network.size (), 1);
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (celerix::dev::genesis->hash ())
				.destination (celerix::keypair ().pub)
				.balance (celerix::dev::constants.genesis_amount - 100)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (celerix::dev::genesis->hash ()))
				.build ();
	send->sideband_set ({});
	auto election (std::make_shared<celerix::election> (node2, send, nullptr, nullptr, celerix::election_behavior::priority));
	// Add a vote for something else, not the winner
	for (auto const & rep : representatives)
	{
		election->set_last_vote (rep.account, { std::chrono::steady_clock::now (), 1, 1 });
	}
	ASSERT_FALSE (solicitor.add (*election));
	ASSERT_FALSE (solicitor.broadcast (*election));
	solicitor.flush ();
	// All requests went through, the last one would normally not go through due to the cap but a vote for a different hash does not count towards the cap
	ASSERT_TIMELY_EQ (6s, max_representatives + 1, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_req, celerix::stat::dir::out));

	solicitor.prepare (representatives);
	auto election2 (std::make_shared<celerix::election> (node2, send, nullptr, nullptr, celerix::election_behavior::priority));
	ASSERT_FALSE (solicitor.add (*election2));
	ASSERT_FALSE (solicitor.broadcast (*election2));

	solicitor.flush ();

	// All requests but one went through, due to the cap
	ASSERT_EQ (2 * max_representatives + 1, node2.stats.count (celerix::stat::type::message, celerix::stat::detail::confirm_req, celerix::stat::dir::out));
}
}
