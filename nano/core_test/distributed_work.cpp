#include <nano/core_test/fakes/work_peer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (distributed_work, stopped)
{
	nano::test::system system (1);
	system.nodes[0]->distributed_work.stop ();
	ASSERT_TRUE (system.nodes[0]->distributed_work.make (nano::work_version::work_1, nano::block_hash (), {}, nano::dev::network_params.work.base, {}));
}

TEST (distributed_work, no_peers)
{
	nano::test::system system (1);
	auto node (system.nodes[0]);
	nano::block_hash hash{ 1 };
	std::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done] (std::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.has_value ());
		work = work_a;
		done = true;
	};
	ASSERT_FALSE (node->distributed_work.make (nano::work_version::work_1, hash, node->config.work_peers, node->network_params.work.base, callback, nano::account ()));
	ASSERT_TIMELY (5s, done);
	ASSERT_GE (nano::dev::network_params.work.difficulty (nano::work_version::work_1, hash, *work), node->network_params.work.base);
	// should only be removed after cleanup
	ASSERT_EQ (1, node->distributed_work.size ());
	while (node->distributed_work.size () > 0)
	{
		node->distributed_work.cleanup_finished ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (distributed_work, no_peers_disabled)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.work_threads = 0;
	auto & node = *system.add_node (node_config);
	ASSERT_TRUE (node.distributed_work.make (nano::work_version::work_1, nano::block_hash (), node.config.work_peers, nano::dev::network_params.work.base, {}));
}

TEST (distributed_work, no_peers_cancel)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.max_work_generate_multiplier = 1e6;
	auto & node = *system.add_node (node_config);
	nano::block_hash hash{ 1 };
	bool done{ false };
	auto callback_to_cancel = [&done] (std::optional<uint64_t> work_a) {
		ASSERT_FALSE (work_a.has_value ());
		done = true;
	};
	ASSERT_FALSE (node.distributed_work.make (nano::work_version::work_1, hash, node.config.work_peers, nano::difficulty::from_multiplier (1e6, node.network_params.work.base), callback_to_cancel));
	ASSERT_EQ (1, node.distributed_work.size ());
	// cleanup should not cancel or remove an ongoing work
	node.distributed_work.cleanup_finished ();
	ASSERT_EQ (1, node.distributed_work.size ());

	// manually cancel
	node.distributed_work.cancel (hash);
	ASSERT_TIMELY (20s, done && node.distributed_work.size () == 0);

	// now using observer
	done = false;
	ASSERT_FALSE (node.distributed_work.make (nano::work_version::work_1, hash, node.config.work_peers, nano::difficulty::from_multiplier (1e6, node.network_params.work.base), callback_to_cancel));
	ASSERT_EQ (1, node.distributed_work.size ());
	node.observers.work_cancel.notify (hash);
	ASSERT_TIMELY (20s, done && node.distributed_work.size () == 0);
}

TEST (distributed_work, no_peers_multi)
{
	nano::test::system system (1);
	auto node (system.nodes[0]);
	nano::block_hash hash{ 1 };
	unsigned total{ 10 };
	std::atomic<unsigned> count{ 0 };
	auto callback = [&count] (std::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.has_value ());
		++count;
	};
	// Test many works for the same root
	for (unsigned i{ 0 }; i < total; ++i)
	{
		ASSERT_FALSE (node->distributed_work.make (nano::work_version::work_1, hash, node->config.work_peers, nano::difficulty::from_multiplier (10, node->network_params.work.base), callback));
	}
	ASSERT_TIMELY_EQ (5s, count, total);
	system.deadline_set (5s);
	while (node->distributed_work.size () > 0)
	{
		node->distributed_work.cleanup_finished ();
		ASSERT_NO_ERROR (system.poll ());
	}
	count = 0;
	// Test many works for different roots
	for (unsigned i{ 0 }; i < total; ++i)
	{
		nano::block_hash hash_i (i + 1);
		ASSERT_FALSE (node->distributed_work.make (nano::work_version::work_1, hash_i, node->config.work_peers, node->network_params.work.base, callback));
	}
	ASSERT_TIMELY_EQ (5s, count, total);
	system.deadline_set (5s);
	while (node->distributed_work.size () > 0)
	{
		node->distributed_work.cleanup_finished ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (distributed_work, peer)
{
	nano::test::system system;
	nano::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	// Disable local work generation
	node_config.work_threads = 0;
	auto node (system.add_node (node_config));
	ASSERT_FALSE (node->local_work_generation_enabled ());
	nano::block_hash hash{ 1 };
	std::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done] (std::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.has_value ());
		work = work_a;
		done = true;
	};
	auto work_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, system.get_available_port (), fake_work_peer_type::good));
	work_peer->start ();
	decltype (node->config.work_peers) peers;
	peers.emplace_back ("::ffff:127.0.0.1", work_peer->port ());
	ASSERT_FALSE (node->distributed_work.make (nano::work_version::work_1, hash, peers, node->network_params.work.base, callback, nano::account ()));
	ASSERT_TIMELY (5s, done);
	ASSERT_GE (nano::dev::network_params.work.difficulty (nano::work_version::work_1, hash, *work), node->network_params.work.base);
	ASSERT_EQ (1, work_peer->generations_good);
	ASSERT_EQ (0, work_peer->generations_bad);
	ASSERT_NO_ERROR (system.poll ());
	ASSERT_EQ (0, work_peer->cancels);
}

// This fails intermittently, the observed behavior is different than what is expected. Disabling because `fake_work_peer` class is not actually used in production.
TEST (distributed_work, DISABLED_peer_malicious)
{
	nano::test::system system (1);
	auto node (system.nodes[0]);
	ASSERT_TRUE (node->local_work_generation_enabled ());
	nano::block_hash hash{ 1 };
	std::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done] (std::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.has_value ());
		work = work_a;
		done = true;
	};
	auto malicious_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, system.get_available_port (), fake_work_peer_type::malicious));
	malicious_peer->start ();
	decltype (node->config.work_peers) peers;
	peers.emplace_back ("::ffff:127.0.0.1", malicious_peer->port ());
	ASSERT_FALSE (node->distributed_work.make (nano::work_version::work_1, hash, peers, node->network_params.work.base, callback, nano::account ()));
	ASSERT_TIMELY (5s, done);
	ASSERT_GE (nano::dev::network_params.work.difficulty (nano::work_version::work_1, hash, *work), node->network_params.work.base);
	ASSERT_TIMELY (5s, malicious_peer->generations_bad >= 1);
	// make sure it was *not* the malicious peer that replied
	ASSERT_EQ (0, malicious_peer->generations_good);
	// initial generation + the second time when it also starts doing local generation
	// it is possible local work generation finishes before the second request is sent, only 1 failure can be required to pass
	ASSERT_GE (malicious_peer->generations_bad, 1);
	// this peer should not receive a cancel
	ASSERT_EQ (0, malicious_peer->cancels);
	// Test again with no local work generation enabled to make sure the malicious peer is sent more than one request
	node->config.work_threads = 0;
	ASSERT_FALSE (node->local_work_generation_enabled ());
	auto malicious_peer2 (std::make_shared<fake_work_peer> (node->work, node->io_ctx, system.get_available_port (), fake_work_peer_type::malicious));
	malicious_peer2->start ();
	peers[0].second = malicious_peer2->port ();
	ASSERT_FALSE (node->distributed_work.make (nano::work_version::work_1, hash, peers, node->network_params.work.base, {}, nano::account ()));
	ASSERT_TIMELY (5s, malicious_peer2->generations_bad >= 2);
	node->distributed_work.cancel (hash);
	ASSERT_EQ (0, malicious_peer2->cancels);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3629
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3630
TEST (distributed_work, DISABLED_peer_multi)
{
	nano::test::system system (1);
	auto node (system.nodes[0]);
	ASSERT_TRUE (node->local_work_generation_enabled ());
	nano::block_hash hash{ 1 };
	std::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done] (std::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.has_value ());
		work = work_a;
		done = true;
	};
	auto good_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, system.get_available_port (), fake_work_peer_type::good));
	auto malicious_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, system.get_available_port (), fake_work_peer_type::malicious));
	auto slow_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, system.get_available_port (), fake_work_peer_type::slow));
	good_peer->start ();
	malicious_peer->start ();
	slow_peer->start ();
	decltype (node->config.work_peers) peers;
	peers.emplace_back ("localhost", malicious_peer->port ());
	peers.emplace_back ("localhost", slow_peer->port ());
	peers.emplace_back ("localhost", good_peer->port ());
	ASSERT_FALSE (node->distributed_work.make (nano::work_version::work_1, hash, peers, node->network_params.work.base, callback, nano::account ()));
	ASSERT_TIMELY (5s, done);
	ASSERT_GE (nano::dev::network_params.work.difficulty (nano::work_version::work_1, hash, *work), node->network_params.work.base);
	ASSERT_TIMELY_EQ (5s, slow_peer->cancels, 1);
	ASSERT_EQ (0, malicious_peer->generations_good);
	ASSERT_EQ (1, malicious_peer->generations_bad);
	ASSERT_EQ (0, malicious_peer->cancels);

	ASSERT_EQ (0, slow_peer->generations_good);
	ASSERT_EQ (0, slow_peer->generations_bad);
	ASSERT_EQ (1, slow_peer->cancels);

	ASSERT_EQ (1, good_peer->generations_good);
	ASSERT_EQ (0, good_peer->generations_bad);
	ASSERT_EQ (0, good_peer->cancels);
}

TEST (distributed_work, fail_resolve)
{
	nano::test::system system (1);
	auto node (system.nodes[0]);
	nano::block_hash hash{ 1 };
	std::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done] (std::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.has_value ());
		work = work_a;
		done = true;
	};
	decltype (node->config.work_peers) peers;
	peers.emplace_back ("beeb.boop.123z", 0);
	ASSERT_FALSE (node->distributed_work.make (nano::work_version::work_1, hash, peers, node->network_params.work.base, callback, nano::account ()));
	ASSERT_TIMELY (5s, done);
	ASSERT_GE (nano::dev::network_params.work.difficulty (nano::work_version::work_1, hash, *work), node->network_params.work.base);
}
