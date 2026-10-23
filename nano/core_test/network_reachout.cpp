#include <nano/node/network.hpp>
#include <nano/node/network_reachout.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/peer_history.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

/*
 * The reachout flags keep every reachout loop from starting, while a node without them runs all three
 */
TEST (network_reachout, disabled)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_reachout = true;
	flags.disable_reachout_preconfigured = true;
	auto & node = *system.add_node (flags);
	auto & control = *system.make_disconnected_node ();

	// The control node, started second, has been round each loop at least once
	ASSERT_TIMELY (5s, control.stats.count (nano::stat::type::network, nano::stat::detail::loop_reachout) >= 1);
	ASSERT_TIMELY (5s, control.stats.count (nano::stat::type::network, nano::stat::detail::loop_reachout_cached) >= 1);
	ASSERT_TIMELY (5s, control.stats.count (nano::stat::type::network, nano::stat::detail::loop_reachout_preconfigured) >= 1);

	// The flagged node, started first, has been round none
	ASSERT_EQ (0, node.stats.count (nano::stat::type::network, nano::stat::detail::loop_reachout));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::network, nano::stat::detail::loop_reachout_cached));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::network, nano::stat::detail::loop_reachout_preconfigured));
}

/*
 * A reachout resolves the address and connects to it, calling back with the endpoint, and a reachout to a peer already connected sends it a keepalive instead
 */
TEST (network_reachout, resolve_and_connect)
{
	nano::test::system system;
	// A long keepalive period leaves the keepalive sent on connect and the reachout's as the only ones
	nano::node_config node_config = system.default_config ();
	node_config.network_params.network.keepalive_period = 10min;
	auto & node = *system.add_node (node_config);
	nano::node_config peer_config = system.default_config ();
	peer_config.network_params.network.keepalive_period = 10min;
	auto peer = system.make_disconnected_node (peer_config, nano::node_flags{});
	auto const port = peer->network.port.load ();
	auto const received = [&peer] () {
		return peer->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in);
	};

	// The first reachout connects and reports the endpoint it reached
	std::atomic<bool> called{ false };
	nano::tcp_endpoint reached;
	std::error_code error;
	node.network_reachout.reachout ("::1", port, [&] (nano::tcp_endpoint const & endpoint, std::error_code ec) {
		reached = endpoint;
		error = ec;
		called = true;
	});
	ASSERT_TIMELY (5s, called);
	ASSERT_FALSE (error);
	ASSERT_EQ (port, reached.port ());
	ASSERT_TIMELY (5s, node.network.find_node_id (peer->get_node_id ()));
	ASSERT_TIMELY_EQ (5s, 1, received ());

	// A reachout to the connected peer sends a keepalive down the existing channel and opens no second one
	node.network_reachout.reachout ("::1", port);
	ASSERT_TIMELY_EQ (5s, 2, received ());
	ASSERT_EQ (1, node.network.size ());
}

/*
 * A reachout trigger wakes the preconfigured reachout loop and no other, so neither keepalive rounds nor cleanup rounds follow the rep crawler's cadence
 */
TEST (network_reachout, trigger_wakes_only_its_loop)
{
	nano::test::system system;
	// Without the rep crawler the test is the only source of triggers, and the long keepalive period keeps that loop from ticking on its own
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	nano::node_config node_config = system.default_config ();
	node_config.network_params.network.keepalive_period = 10min;
	auto & node = *system.add_node (node_config, flags);
	auto const count = [&node] (nano::stat::detail detail) {
		return node.stats.count (nano::stat::type::network, detail);
	};
	auto const start = std::chrono::steady_clock::now ();
	auto const cleanup_before = count (nano::stat::detail::loop_cleanup);
	auto const preconfigured_before = count (nano::stat::detail::loop_reachout_preconfigured);

	// A hundred triggers, each landing while the loops wait
	for (auto i = 0; i < 100; ++i)
	{
		node.network_reachout.trigger ();
		std::this_thread::sleep_for (10ms);
	}
	ASSERT_EQ (100, count (nano::stat::detail::trigger_reachout));

	// The preconfigured loop went round and the keepalive loop never did
	ASSERT_TIMELY (5s, count (nano::stat::detail::loop_reachout_preconfigured) > preconfigured_before);
	ASSERT_ALWAYS_EQ (500ms, 0, count (nano::stat::detail::loop_keepalive));

	// The cleanup loop kept to its one tick a second, counted against the time that passed rather than the time the sleeps asked for
	auto const cleanup_ticks = count (nano::stat::detail::loop_cleanup) - cleanup_before;
	auto const elapsed = std::chrono::steady_clock::now () - start;
	ASSERT_LE (cleanup_ticks, static_cast<uint64_t> (elapsed / 1s) + 1);
}

/*
 * Reachout triggers do not age the duplicate filter, which keeps counting its cutoff in seconds
 */
TEST (network_reachout, trigger_keeps_filter_epoch)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	nano::node_config node_config = system.default_config ();
	node_config.network->duplicate_filter_cutoff = 3;
	auto & node = *system.add_node (node_config, flags);
	std::vector<uint8_t> const bytes (32, 1);
	ASSERT_FALSE (node.network.filter.apply (bytes.data (), bytes.size ()));

	// Fifty triggers in half a second leave the entry in place, where fifty cleanup rounds would have aged it out
	for (auto i = 0; i < 50; ++i)
	{
		node.network_reachout.trigger ();
		std::this_thread::sleep_for (10ms);
	}
	ASSERT_TRUE (node.network.filter.check (bytes.data (), bytes.size ()));

	// It still expires with the cutoff, counted in real seconds
	ASSERT_TIMELY (10s, !node.network.filter.check (bytes.data (), bytes.size ()));
}

/*
 * A reachout trigger makes the preconfigured loop reach out at once instead of at its next period
 */
TEST (network_reachout, trigger_reaches_out)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	nano::node_config node_config = system.default_config ();
	// A peer address that resolves, with nothing listening on the default port, and periods too long to fire during the test
	node_config.preconfigured_peers = { "::1" };
	node_config.network_params.network.reachout_preconfigured_period = 10min;
	node_config.network_params.network.reachout_preconfigured_warmup_period = 10min;
	auto & node = *system.add_node (node_config, flags);
	auto const count = [&node] () {
		return node.stats.count (nano::stat::type::network, nano::stat::detail::reachout_preconfigured);
	};

	// The first round reaches out once and the next waits for the period
	ASSERT_TIMELY_EQ (5s, 1, count ());
	ASSERT_ALWAYS_EQ (1s, 1, count ());

	// A trigger brings the next round forward
	node.network_reachout.trigger ();
	ASSERT_TIMELY_EQ (5s, 2, count ());
}

/*
 * Peers whose channels died are reconnected from the peer history cache
 */
TEST (network_reachout, reconnect_cached)
{
	nano::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();
	ASSERT_EQ (1, node1.network.size ());
	ASSERT_EQ (1, node2.network.size ());
	auto const channel1 = node1.network.list ().front ();
	auto const channel2 = node2.network.list ().front ();
	auto const has_channel = [] (nano::node & node, std::shared_ptr<nano::transport::channel> const & channel) {
		auto const channels = node.network.list ();
		return std::find (channels.begin (), channels.end (), channel) != channels.end ();
	};

	// Each node caches its one peer
	node1.peer_history.trigger ();
	node2.peer_history.trigger ();
	ASSERT_TIMELY_EQ (5s, 1, node1.peer_history.size ());
	ASSERT_TIMELY_EQ (5s, 1, node2.peer_history.size ());

	// Both channels are closed and drop out of the containers
	channel1->close ();
	channel2->close ();
	ASSERT_TIMELY (5s, !has_channel (node1, channel1));
	ASSERT_TIMELY (5s, !has_channel (node2, channel2));

	// The cached reachout brings the two back together
	ASSERT_TIMELY_EQ (5s, 1, node1.network.size ());
	ASSERT_TIMELY_EQ (5s, 1, node2.network.size ());
	ASSERT_TRUE (node1.network.find_node_id (node2.get_node_id ()));
	ASSERT_TRUE (node2.network.find_node_id (node1.get_node_id ()));
}
