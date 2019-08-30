#include <nano/core_test/testutil.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (distributed_work, no_peers)
{
	nano::system system (24000, 1);
	auto node (system.nodes[0]);
	nano::block_hash hash;
	boost::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done](boost::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.is_initialized ());
		work = work_a;
		done = true;
	};
	node->distributed_work.make (hash, callback, node->network_params.network.publish_threshold);
	system.deadline_set (5s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_FALSE (nano::work_validate (hash, *work));
	// should only be removed after cleanup
	ASSERT_EQ (1, node->distributed_work.work.size ());
	node->distributed_work.cleanup_finished ();
	ASSERT_EQ (0, node->distributed_work.work.size ());
}

TEST (distributed_work, no_peers_cancel)
{
	nano::system system (24000, 1);
	auto node (system.nodes[0]);
	nano::block_hash hash;
	bool done{ false };
	auto callback_to_cancel = [&done](boost::optional<uint64_t> work_a) {
		ASSERT_FALSE (work_a.is_initialized ());
		done = true;
	};
	node->distributed_work.make (hash, callback_to_cancel, nano::difficulty::from_multiplier (1000000, node->network_params.network.publish_threshold));
	ASSERT_EQ (1, node->distributed_work.work.size ());
	// cleanup should not cancel or remove an ongoing work
	node->distributed_work.cleanup_finished ();
	ASSERT_EQ (1, node->distributed_work.work.size ());

	// manually cancel
	node->distributed_work.cancel (hash, true); // forces local stop
	system.deadline_set (5s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node->distributed_work.work.empty ());

	// now using observer
	done = false;
	node->distributed_work.make (hash, callback_to_cancel, nano::difficulty::from_multiplier (1000000, node->network_params.network.publish_threshold));
	ASSERT_EQ (1, node->distributed_work.work.size ());
	node->observers.work_cancel.notify (hash);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node->distributed_work.work.empty ());
}

TEST (distributed_work, no_peers_multi)
{
	nano::system system (24000, 1);
	auto node (system.nodes[0]);
	nano::block_hash hash;
	unsigned total{ 10 };
	std::atomic<unsigned> count{ 0 };
	auto callback = [&count](boost::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.is_initialized ());
		++count;
	};
	// Test many works for the same root
	for (unsigned i{ 0 }; i < total; ++i)
	{
		node->distributed_work.make (hash, callback, nano::difficulty::from_multiplier (10, node->network_params.network.publish_threshold));
	}
	// 1 root, and _total_ requests for that root are expected
	ASSERT_EQ (1, node->distributed_work.work.size ());
	{
		auto requests (node->distributed_work.work.begin ());
		ASSERT_EQ (hash, requests->first);
		ASSERT_EQ (total, requests->second.size ());
	}
	system.deadline_set (5s);
	while (count < total)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node->distributed_work.cleanup_finished ();
	ASSERT_EQ (0, node->distributed_work.work.size ());
	count = 0;
	// Test many works for different roots
	for (unsigned i{ 0 }; i < total; ++i)
	{
		nano::block_hash hash_i (i + 1);
		node->distributed_work.make (hash_i, callback, node->network_params.network.publish_threshold);
	}
	// 10 roots expected with 1 work each, but some may have completed so test for some
	ASSERT_GT (node->distributed_work.work.size (), 5);
	for (auto & requests : node->distributed_work.work)
	{
		ASSERT_EQ (1, requests.second.size ());
	}
	system.deadline_set (5s);
	while (count < total)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node->distributed_work.cleanup_finished ();
	ASSERT_EQ (0, node->distributed_work.work.size ());
	count = 0;
}
