#include <nano/node/vote_generator.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/voting_policy.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <set>

using namespace std::chrono_literals;

/*
 * vote_generator_index
 */

TEST (vote_generator_index, empty_state)
{
	nano::vote_generator_index index{ 128 };
	ASSERT_TRUE (index.empty ());
	ASSERT_EQ (index.size (), 0);

	// next_batch on empty returns nothing
	auto batch = index.next_batch (10);
	ASSERT_TRUE (batch.empty ());

	// next_batch(0) after push returns nothing but does not consume
	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	ASSERT_TRUE (index.push (root1, nano::block_hash{ 10 }, 0));
	auto batch0 = index.next_batch (0);
	ASSERT_TRUE (batch0.empty ());
	ASSERT_EQ (index.size (), 1);
}

TEST (vote_generator_index, push_and_retrieve)
{
	nano::vote_generator_index index{ 128 };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto root3 = nano::qualified_root{ nano::root{ 3 }, nano::block_hash{ 3 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };
	auto hash3 = nano::block_hash{ 30 };

	ASSERT_TRUE (index.push (root1, hash1, 0));
	ASSERT_TRUE (index.push (root2, hash2, 0));
	ASSERT_TRUE (index.push (root3, hash3, 0));
	ASSERT_EQ (index.size (), 3);
	ASSERT_FALSE (index.empty ());

	// Request more than available
	auto batch = index.next_batch (10);
	ASSERT_EQ (batch.size (), 3);

	// Verify all entries present
	auto find = [&] (nano::qualified_root const & root) {
		return std::find_if (batch.begin (), batch.end (), [&] (auto const & e) { return e.first == root; });
	};
	auto it1 = find (root1);
	ASSERT_NE (it1, batch.end ());
	ASSERT_EQ (it1->second, hash1);
	auto it2 = find (root2);
	ASSERT_NE (it2, batch.end ());
	ASSERT_EQ (it2->second, hash2);
	auto it3 = find (root3);
	ASSERT_NE (it3, batch.end ());
	ASSERT_EQ (it3->second, hash3);

	ASSERT_TRUE (index.empty ());
	ASSERT_EQ (index.size (), 0);
}

TEST (vote_generator_index, duplicate_detection)
{
	nano::vote_generator_index index{ 128 };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto hash1 = nano::block_hash{ 10 };

	ASSERT_TRUE (index.push (root1, hash1, 0));
	ASSERT_EQ (index.size (), 1);

	// Exact duplicate, same bucket
	ASSERT_FALSE (index.push (root1, hash1, 0));
	ASSERT_EQ (index.size (), 1);

	// Exact duplicate, different bucket — dedup is by root, not bucket
	ASSERT_FALSE (index.push (root1, hash1, 1));
	ASSERT_EQ (index.size (), 1);

	auto batch = index.next_batch (10);
	ASSERT_EQ (batch.size (), 1);
	ASSERT_EQ (batch[0].first, root1);
	ASSERT_EQ (batch[0].second, hash1);
}

TEST (vote_generator_index, replacement_and_staleness)
{
	nano::vote_generator_index index{ 128 };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto hash_a = nano::block_hash{ 10 };
	auto hash_b = nano::block_hash{ 20 };
	auto hash_c = nano::block_hash{ 30 };

	// Push and replace twice
	ASSERT_TRUE (index.push (root1, hash_a, 0));
	ASSERT_TRUE (index.push (root1, hash_b, 0));
	ASSERT_EQ (index.size (), 1);
	ASSERT_TRUE (index.push (root1, hash_c, 0));
	ASSERT_EQ (index.size (), 1);

	// Push a separate root
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto hash_d = nano::block_hash{ 40 };
	ASSERT_TRUE (index.push (root2, hash_d, 0));
	ASSERT_EQ (index.size (), 2);

	// Extract: stale entries for hash_a and hash_b should be skipped
	auto batch = index.next_batch (10);
	ASSERT_EQ (batch.size (), 2);

	auto find = [&] (nano::qualified_root const & root) {
		return std::find_if (batch.begin (), batch.end (), [&] (auto const & e) { return e.first == root; });
	};
	auto it1 = find (root1);
	ASSERT_NE (it1, batch.end ());
	ASSERT_EQ (it1->second, hash_c);
	auto it2 = find (root2);
	ASSERT_NE (it2, batch.end ());
	ASSERT_EQ (it2->second, hash_d);

	ASSERT_TRUE (index.empty ());
}

TEST (vote_generator_index, queue_full)
{
	nano::vote_generator_index index{ 2 }; // max 2 per bucket

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto root3 = nano::qualified_root{ nano::root{ 3 }, nano::block_hash{ 3 } };

	// Fill bucket 0
	ASSERT_TRUE (index.push (root1, nano::block_hash{ 10 }, 0));
	ASSERT_TRUE (index.push (root2, nano::block_hash{ 20 }, 0));
	ASSERT_EQ (index.size (), 2);

	// Bucket 0 full
	ASSERT_FALSE (index.push (root3, nano::block_hash{ 30 }, 0));
	ASSERT_EQ (index.size (), 2);

	// Different bucket still accepts
	ASSERT_TRUE (index.push (root3, nano::block_hash{ 30 }, 1));
	ASSERT_EQ (index.size (), 3);

	auto batch = index.next_batch (10);
	ASSERT_EQ (batch.size (), 3);
	ASSERT_TRUE (index.empty ());
}

TEST (vote_generator_index, multi_bucket_fairness)
{
	nano::vote_generator_index index{ 128 };

	// 3 entries in bucket 0
	for (uint64_t i = 1; i <= 3; ++i)
	{
		auto root = nano::qualified_root{ nano::root{ i }, nano::block_hash{ i } };
		ASSERT_TRUE (index.push (root, nano::block_hash{ i * 10 }, 0));
	}
	// 3 entries in bucket 1
	for (uint64_t i = 4; i <= 6; ++i)
	{
		auto root = nano::qualified_root{ nano::root{ i }, nano::block_hash{ i } };
		ASSERT_TRUE (index.push (root, nano::block_hash{ i * 10 }, 1));
	}
	ASSERT_EQ (index.size (), 6);

	// Extract 4: fair round-robin with priority=1 should give 2 from each bucket
	auto batch = index.next_batch (4);
	ASSERT_EQ (batch.size (), 4);

	// Roots 1-3 were pushed to bucket 0, roots 4-6 to bucket 1
	std::set<nano::qualified_root> bucket0_roots, bucket1_roots;
	for (uint64_t i = 1; i <= 3; ++i)
	{
		bucket0_roots.insert (nano::qualified_root{ nano::root{ i }, nano::block_hash{ i } });
	}
	for (uint64_t i = 4; i <= 6; ++i)
	{
		bucket1_roots.insert (nano::qualified_root{ nano::root{ i }, nano::block_hash{ i } });
	}
	int bucket0_count = 0;
	int bucket1_count = 0;
	for (auto const & [root, hash] : batch)
	{
		if (bucket0_roots.count (root))
		{
			++bucket0_count;
		}
		else if (bucket1_roots.count (root))
		{
			++bucket1_count;
		}
	}
	ASSERT_EQ (bucket0_count, 2);
	ASSERT_EQ (bucket1_count, 2);

	// Extract remaining
	auto rest = index.next_batch (10);
	ASSERT_EQ (rest.size (), 2);
	ASSERT_TRUE (index.empty ());
}

TEST (vote_generator_index, replacement_when_bucket_full)
{
	nano::vote_generator_index index{ 2 }; // max 2 per bucket

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto hash_a = nano::block_hash{ 10 };
	auto hash_b = nano::block_hash{ 20 };
	auto hash_c = nano::block_hash{ 30 };

	// Fill bucket 0
	ASSERT_TRUE (index.push (root1, hash_a, 0));
	ASSERT_TRUE (index.push (root2, hash_b, 0));

	// Replace root1: dedup updates to hash_c, but queue.push fails (bucket full with stale + valid entries)
	ASSERT_FALSE (index.push (root1, hash_c, 0));

	// Dedup was updated despite push failure: root1 now maps to hash_c
	// Queue still has: root1/hash_a(stale), root2/hash_b(valid)
	ASSERT_EQ (index.size (), 2);

	// Extract: root1/hash_a is stale (dedup says hash_c) → skipped; root2/hash_b → valid
	auto batch = index.next_batch (10);
	ASSERT_EQ (batch.size (), 1);
	ASSERT_EQ (batch[0].first, root2);
	ASSERT_EQ (batch[0].second, hash_b);

	// root1 is a zombie: exists in dedup but not in queue
	ASSERT_EQ (index.size (), 1);
	ASSERT_FALSE (index.empty ());
	auto empty_batch = index.next_batch (10);
	ASSERT_TRUE (empty_batch.empty ());
}

/*
 * vote_broadcast_index
 */

TEST (vote_broadcast_index, empty_state)
{
	nano::vote_broadcast_index index{ 128 };
	ASSERT_TRUE (index.empty ());
	ASSERT_EQ (index.size (), 0);

	auto batch = index.next_batch (10);
	ASSERT_TRUE (batch.empty ());

	ASSERT_FALSE (index.erase (nano::qualified_root{ 1 }));
}

TEST (vote_broadcast_index, push_and_fifo_extraction)
{
	nano::vote_broadcast_index index{ 128 };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto root3 = nano::qualified_root{ nano::root{ 3 }, nano::block_hash{ 3 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };
	auto hash3 = nano::block_hash{ 30 };

	ASSERT_TRUE (index.push (root1, nano::vote_permit::dummy (root1, hash1, nano::vote_type::normal)));
	ASSERT_TRUE (index.push (root2, nano::vote_permit::dummy (root2, hash2, nano::vote_type::normal)));
	ASSERT_TRUE (index.push (root3, nano::vote_permit::dummy (root3, hash3, nano::vote_type::normal)));
	ASSERT_EQ (index.size (), 3);

	// Partial extraction — FIFO order
	auto batch1 = index.next_batch (2);
	ASSERT_EQ (batch1.size (), 2);
	ASSERT_EQ (batch1[0].hash (), hash1);
	ASSERT_EQ (batch1[1].hash (), hash2);
	ASSERT_EQ (index.size (), 1);

	// Request more than remaining
	auto batch2 = index.next_batch (10);
	ASSERT_EQ (batch2.size (), 1);
	ASSERT_EQ (batch2[0].hash (), hash3);
	ASSERT_TRUE (index.empty ());
}

TEST (vote_broadcast_index, duplicate_detection)
{
	nano::vote_broadcast_index index{ 128 };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };

	ASSERT_TRUE (index.push (root1, nano::vote_permit::dummy (root1, hash1, nano::vote_type::normal)));
	ASSERT_EQ (index.size (), 1);

	// Exact duplicate
	ASSERT_FALSE (index.push (root1, nano::vote_permit::dummy (root1, hash1, nano::vote_type::normal)));
	ASSERT_EQ (index.size (), 1);

	// Push another root, verify FIFO: root1 still before root2
	ASSERT_TRUE (index.push (root2, nano::vote_permit::dummy (root2, hash2, nano::vote_type::normal)));
	auto batch = index.next_batch (10);
	ASSERT_EQ (batch.size (), 2);
	ASSERT_EQ (batch[0].hash (), hash1);
	ASSERT_EQ (batch[1].hash (), hash2);
}

TEST (vote_broadcast_index, replacement_reorders_to_back)
{
	nano::vote_broadcast_index index{ 128 };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto root3 = nano::qualified_root{ nano::root{ 3 }, nano::block_hash{ 3 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };
	auto hash3 = nano::block_hash{ 30 };
	auto hash1_new = nano::block_hash{ 11 };

	ASSERT_TRUE (index.push (root1, nano::vote_permit::dummy (root1, hash1, nano::vote_type::normal)));
	ASSERT_TRUE (index.push (root2, nano::vote_permit::dummy (root2, hash2, nano::vote_type::normal)));
	ASSERT_TRUE (index.push (root3, nano::vote_permit::dummy (root3, hash3, nano::vote_type::normal)));

	// Replace root1 with different hash — moves to back
	ASSERT_TRUE (index.push (root1, nano::vote_permit::dummy (root1, hash1_new, nano::vote_type::normal)));
	ASSERT_EQ (index.size (), 3);

	// FIFO: root2, root3, root1(new)
	auto batch = index.next_batch (10);
	ASSERT_EQ (batch.size (), 3);
	ASSERT_EQ (batch[0].qualified_root (), root2);
	ASSERT_EQ (batch[0].hash (), hash2);
	ASSERT_EQ (batch[1].qualified_root (), root3);
	ASSERT_EQ (batch[1].hash (), hash3);
	ASSERT_EQ (batch[2].qualified_root (), root1);
	ASSERT_EQ (batch[2].hash (), hash1_new);
}

TEST (vote_broadcast_index, max_size_enforcement)
{
	nano::vote_broadcast_index index{ 3 };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto root3 = nano::qualified_root{ nano::root{ 3 }, nano::block_hash{ 3 } };
	auto root4 = nano::qualified_root{ nano::root{ 4 }, nano::block_hash{ 4 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };
	auto hash3 = nano::block_hash{ 30 };
	auto hash4 = nano::block_hash{ 40 };
	auto hash2_new = nano::block_hash{ 21 };

	ASSERT_TRUE (index.push (root1, nano::vote_permit::dummy (root1, hash1, nano::vote_type::normal)));
	ASSERT_TRUE (index.push (root2, nano::vote_permit::dummy (root2, hash2, nano::vote_type::normal)));
	ASSERT_TRUE (index.push (root3, nano::vote_permit::dummy (root3, hash3, nano::vote_type::normal)));

	// Full — new root rejected
	ASSERT_FALSE (index.push (root4, nano::vote_permit::dummy (root4, hash4, nano::vote_type::normal)));
	ASSERT_EQ (index.size (), 3);

	// Replacement at capacity works (erase + push_back keeps size at max)
	ASSERT_TRUE (index.push (root2, nano::vote_permit::dummy (root2, hash2_new, nano::vote_type::normal)));
	ASSERT_EQ (index.size (), 3);

	// FIFO after replacement: root1, root3, root2(new)
	auto batch = index.next_batch (10);
	ASSERT_EQ (batch.size (), 3);
	ASSERT_EQ (batch[0].qualified_root (), root1);
	ASSERT_EQ (batch[1].qualified_root (), root3);
	ASSERT_EQ (batch[2].qualified_root (), root2);
	ASSERT_EQ (batch[2].hash (), hash2_new);
}

TEST (vote_broadcast_index, erase)
{
	nano::vote_broadcast_index index{ 128 };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto root3 = nano::qualified_root{ nano::root{ 3 }, nano::block_hash{ 3 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };
	auto hash3 = nano::block_hash{ 30 };

	ASSERT_TRUE (index.push (root1, nano::vote_permit::dummy (root1, hash1, nano::vote_type::normal)));
	ASSERT_TRUE (index.push (root2, nano::vote_permit::dummy (root2, hash2, nano::vote_type::normal)));
	ASSERT_TRUE (index.push (root3, nano::vote_permit::dummy (root3, hash3, nano::vote_type::normal)));

	// Erase middle entry
	ASSERT_TRUE (index.erase (root2));
	ASSERT_EQ (index.size (), 2);

	// Non-existent root
	ASSERT_FALSE (index.erase (nano::qualified_root{ nano::root{ 99 }, nano::block_hash{ 99 } }));
	ASSERT_EQ (index.size (), 2);

	// Double erase
	ASSERT_FALSE (index.erase (root2));

	// FIFO: root1, root3
	auto batch = index.next_batch (10);
	ASSERT_EQ (batch.size (), 2);
	ASSERT_EQ (batch[0].qualified_root (), root1);
	ASSERT_EQ (batch[1].qualified_root (), root3);
}

/*
 * vote_generator_verifier
 */

TEST (vote_generator_verifier, push_and_process)
{
	nano::test::system system;

	std::atomic<size_t> processed{ 0 };
	std::vector<nano::vote_generator_verifier::entry> received;
	nano::mutex received_mutex;

	nano::vote_generator_verifier verifier{ 128, 256, 1, nano::thread_role::name::voting_normal_processing };
	verifier.process_batch = [&] (auto batch) {
		auto count = batch.size ();
		{
			nano::lock_guard<nano::mutex> lock{ received_mutex };
			for (auto & e : batch)
			{
				received.push_back (std::move (e));
			}
		}
		processed += count;
	};

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto root3 = nano::qualified_root{ nano::root{ 3 }, nano::block_hash{ 3 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };
	auto hash3 = nano::block_hash{ 30 };

	// Push before start
	ASSERT_TRUE (verifier.push (root1, hash1, 0));
	ASSERT_TRUE (verifier.push (root2, hash2, 0));
	ASSERT_TRUE (verifier.push (root3, hash3, 0));
	ASSERT_EQ (verifier.size (), 3);
	ASSERT_FALSE (verifier.empty ());

	nano::test::start_stop_guard guard{ verifier };

	ASSERT_TIMELY_EQ (5s, processed.load (), 3);
	ASSERT_TRUE (verifier.empty ());
	ASSERT_EQ (verifier.size (), 0);

	// Verify all entries received
	nano::lock_guard<nano::mutex> lock{ received_mutex };
	ASSERT_EQ (received.size (), 3);
	auto find = [&] (nano::qualified_root const & root) {
		return std::find_if (received.begin (), received.end (), [&] (auto const & e) { return e.first == root; });
	};
	auto it1 = find (root1);
	ASSERT_NE (it1, received.end ());
	ASSERT_EQ (it1->second, hash1);
	auto it2 = find (root2);
	ASSERT_NE (it2, received.end ());
	ASSERT_EQ (it2->second, hash2);
	auto it3 = find (root3);
	ASSERT_NE (it3, received.end ());
	ASSERT_EQ (it3->second, hash3);
}

TEST (vote_generator_verifier, batch_size_limiting)
{
	nano::test::system system;

	std::atomic<size_t> processed{ 0 };
	std::atomic<size_t> max_batch_seen{ 0 };

	nano::vote_generator_verifier verifier{ 128, 4, 1, nano::thread_role::name::voting_normal_processing };
	verifier.process_batch = [&] (auto batch) {
		auto count = batch.size ();
		size_t prev = max_batch_seen.load ();
		while (prev < count && !max_batch_seen.compare_exchange_weak (prev, count))
			;
		processed += count;

		std::this_thread::sleep_for (std::chrono::milliseconds{ 15 });
	};

	for (uint64_t i = 1; i <= 10; ++i)
	{
		auto root = nano::qualified_root{ nano::root{ i }, nano::block_hash{ i } };
		ASSERT_TRUE (verifier.push (root, nano::block_hash{ i * 10 }, 0));
	}
	ASSERT_EQ (verifier.size (), 10);

	nano::test::start_stop_guard guard{ verifier };

	ASSERT_TIMELY_EQ (5s, processed.load (), 10);
	ASSERT_LE (max_batch_seen.load (), 4);
	ASSERT_TRUE (verifier.empty ());
}

/*
 * vote_generator_broadcaster
 */

TEST (vote_generator_broadcaster, threshold_trigger)
{
	nano::test::system system;

	std::atomic<size_t> broadcast_count{ 0 };
	std::vector<nano::vote_permit> received;
	nano::mutex received_mutex;

	nano::vote_generator_broadcaster broadcaster{ 128, 3, 10s, nano::thread_role::name::voting_normal_broadcast };
	broadcaster.check_capacity = [] () { return true; };
	broadcaster.broadcast_batch = [&] (auto batch) {
		auto count = batch.size ();
		{
			nano::lock_guard<nano::mutex> lock{ received_mutex };
			for (auto & p : batch)
			{
				received.push_back (std::move (p));
			}
		}
		broadcast_count += count;
	};

	nano::test::start_stop_guard guard{ broadcaster };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto root3 = nano::qualified_root{ nano::root{ 3 }, nano::block_hash{ 3 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };
	auto hash3 = nano::block_hash{ 30 };

	ASSERT_TRUE (broadcaster.push (root1, nano::vote_permit::dummy (root1, hash1, nano::vote_type::normal)));
	ASSERT_TRUE (broadcaster.push (root2, nano::vote_permit::dummy (root2, hash2, nano::vote_type::normal)));
	ASSERT_TRUE (broadcaster.push (root3, nano::vote_permit::dummy (root3, hash3, nano::vote_type::normal)));

	// Should trigger quickly via threshold (batch_threshold=3), not waiting 10s delay
	ASSERT_TIMELY_EQ (5s, broadcast_count.load (), 3);
	ASSERT_TRUE (broadcaster.empty ());

	// Verify FIFO order
	nano::lock_guard<nano::mutex> lock{ received_mutex };
	ASSERT_EQ (received.size (), 3);
	ASSERT_EQ (received[0].hash (), hash1);
	ASSERT_EQ (received[1].hash (), hash2);
	ASSERT_EQ (received[2].hash (), hash3);
}

TEST (vote_generator_broadcaster, timer_trigger)
{
	nano::test::system system;

	std::atomic<size_t> broadcast_count{ 0 };
	std::atomic<size_t> broadcast_batch{ 0 };

	nano::vote_generator_broadcaster broadcaster{ 128, 100, 250ms, nano::thread_role::name::voting_normal_broadcast };
	broadcaster.check_capacity = [] () { return true; };
	broadcaster.broadcast_batch = [&] (auto batch) {
		broadcast_count += batch.size ();
		broadcast_batch = batch.size ();
	};

	nano::test::start_stop_guard guard{ broadcaster };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };

	ASSERT_TRUE (broadcaster.push (root1, nano::vote_permit::dummy (root1, hash1, nano::vote_type::normal)));
	ASSERT_TRUE (broadcaster.push (root2, nano::vote_permit::dummy (root2, hash2, nano::vote_type::normal)));

	// Threshold is 100, so 2 entries won't trigger via threshold
	// Timer with 250ms delay should trigger broadcast
	ASSERT_TIMELY_EQ (1s, broadcast_count.load (), 2);
	ASSERT_TRUE (broadcaster.empty ());
	ASSERT_EQ (broadcast_batch.load (), 2);
}

TEST (vote_generator_broadcaster, check_capacity_backpressure)
{
	nano::test::system system;

	std::atomic<size_t> broadcast_count{ 0 };
	std::atomic<bool> allow_capacity{ false };

	nano::vote_generator_broadcaster broadcaster{ 128, 1, 10ms, nano::thread_role::name::voting_normal_broadcast };
	broadcaster.check_capacity = [&] () { return allow_capacity.load (); };
	broadcaster.broadcast_batch = [&] (auto batch) {
		broadcast_count += batch.size ();
	};

	nano::test::start_stop_guard guard{ broadcaster };

	auto root1 = nano::qualified_root{ nano::root{ 1 }, nano::block_hash{ 1 } };
	auto root2 = nano::qualified_root{ nano::root{ 2 }, nano::block_hash{ 2 } };
	auto hash1 = nano::block_hash{ 10 };
	auto hash2 = nano::block_hash{ 20 };

	ASSERT_TRUE (broadcaster.push (root1, nano::vote_permit::dummy (root1, hash1, nano::vote_type::normal)));
	ASSERT_TRUE (broadcaster.push (root2, nano::vote_permit::dummy (root2, hash2, nano::vote_type::normal)));

	// Broadcast blocked by capacity
	ASSERT_NEVER (500ms, broadcast_count.load () > 0);

	// Allow broadcast
	allow_capacity = true;

	ASSERT_TIMELY_EQ (5s, broadcast_count.load (), 2);
	ASSERT_TRUE (broadcaster.empty ());
}

/*
 * vote_generator
 */

// Confirmed block produces a normal vote broadcast with correct contents
TEST (vote_generator, basic_broadcast)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	auto blocks = nano::test::setup_chain (system, node, 1);
	auto & block = blocks.front ();

	nano::shared_locked<std::vector<std::shared_ptr<nano::vote>>> votes;
	node.observers.vote.add ([votes] (std::shared_ptr<nano::vote> const & vote, std::shared_ptr<nano::transport::channel> const &, nano::vote_source, nano::vote_code) {
		votes->push_back (vote);
	});

	node.vote_generator.vote_normal (block->qualified_root (), block->hash (), 0);
	ASSERT_TIMELY_EQ (5s, votes->size (), 1);
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::broadcast), 1);

	auto locked = votes.lock ();
	ASSERT_EQ (nano::dev::genesis_key.pub, locked->at (0)->account);
	ASSERT_EQ (1, locked->at (0)->hashes.size ());
	ASSERT_EQ (block->hash (), locked->at (0)->hashes[0]);
	ASSERT_FALSE (locked->at (0)->is_final ());
}

// Each wallet representative produces its own vote broadcast for the same block
TEST (vote_generator, multiple_representatives)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto & wallet = *system.wallet (0);

	// Setup representatives without wallet keys to avoid background voting during setup
	nano::keypair key1, key2, key3;
	auto const amount = 100 * nano::Knano_ratio;
	auto rep1 = nano::test::setup_rep (system, node, amount, nano::dev::genesis_key);
	auto rep2 = nano::test::setup_rep (system, node, amount, nano::dev::genesis_key);
	auto rep3 = nano::test::setup_rep (system, node, amount, nano::dev::genesis_key);

	// Create a confirmed block to vote on before adding wallet keys
	auto blocks = nano::test::setup_chain (system, node, 1);
	auto & block = blocks.front ();

	// Now insert all keys into wallet so they become voting representatives
	wallet.insert_adhoc (nano::dev::genesis_key.prv);
	wallet.insert_adhoc (rep1.prv);
	wallet.insert_adhoc (rep2.prv);
	wallet.insert_adhoc (rep3.prv);
	node.wallets.refresh_reps ();
	ASSERT_EQ (4, node.wallets.reps ().voting);

	nano::shared_locked<std::vector<std::shared_ptr<nano::vote>>> votes;
	node.observers.vote.add ([votes] (std::shared_ptr<nano::vote> const & vote, std::shared_ptr<nano::transport::channel> const &, nano::vote_source, nano::vote_code) {
		votes->push_back (vote);
	});

	node.vote_generator.vote_normal (block->qualified_root (), block->hash (), 0);

	// 4 representatives should each produce a vote
	ASSERT_TIMELY_EQ (5s, votes->size (), 4);
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::broadcast), 4);

	auto locked = votes.lock ();
	std::set<nano::account> signers;
	for (auto const & vote : locked.get ())
	{
		ASSERT_TRUE (std::find (vote->hashes.begin (), vote->hashes.end (), block->hash ()) != vote->hashes.end ());
		signers.insert (vote->account);
	}
	ASSERT_EQ (4, signers.size ());
	ASSERT_TRUE (signers.count (nano::dev::genesis_key.pub));
	ASSERT_TRUE (signers.count (rep1.pub));
	ASSERT_TRUE (signers.count (rep2.pub));
	ASSERT_TRUE (signers.count (rep3.pub));
}

// Normal vote for a root with an existing final vote record is upgraded and routed to final broadcaster
TEST (vote_generator, normal_upgraded_to_final)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	auto blocks = nano::test::setup_chain (system, node, 1);
	auto & block = blocks.front ();

	nano::shared_locked<std::vector<std::shared_ptr<nano::vote>>> votes;
	node.observers.vote.add ([votes] (std::shared_ptr<nano::vote> const & vote, std::shared_ptr<nano::transport::channel> const &, nano::vote_source, nano::vote_code) {
		votes->push_back (vote);
	});

	// Issue a final vote first to establish the final vote record in the ledger
	node.vote_generator.vote_final (block->qualified_root (), block->hash (), 0);
	ASSERT_TIMELY_EQ (5s, votes->size (), 1);
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_generator_final, nano::stat::detail::broadcast), 1);
	ASSERT_TRUE (votes->at (0)->is_final ());

	// Clear stats to isolate the next vote's broadcasts
	node.stats.clear ();

	// Now issue a NORMAL vote for the same root
	// voting_policy::vote() detects the existing final vote record and upgrades the permit to final
	node.vote_generator.vote_normal (block->qualified_root (), block->hash (), 0);
	ASSERT_TIMELY_EQ (5s, votes->size (), 2);

	// The upgraded permit should go through the final broadcaster, not the normal one
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_generator_final, nano::stat::detail::broadcast), 1);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::broadcast));

	// The upgraded vote should also be final
	auto locked = votes.lock ();
	ASSERT_TRUE (locked->at (1)->is_final ());
	ASSERT_EQ (block->hash (), locked->at (1)->hashes[0]);
	ASSERT_EQ (nano::dev::genesis_key.pub, locked->at (1)->account);
}

// Vote for a nonexistent block is skipped without blocking subsequent votes
TEST (vote_generator, block_missing_skipped)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	nano::shared_locked<std::vector<std::shared_ptr<nano::vote>>> votes;
	node.observers.vote.add ([votes] (std::shared_ptr<nano::vote> const & vote, std::shared_ptr<nano::transport::channel> const &, nano::vote_source, nano::vote_code) {
		votes->push_back (vote);
	});

	// Submit a vote for a nonexistent block
	auto fake_root = nano::qualified_root{ nano::root{ 999 }, nano::block_hash{ 999 } };
	auto fake_hash = nano::block_hash{ 12345 };
	node.vote_generator.vote_normal (fake_root, fake_hash, 0);

	// Submit a vote for a real confirmed block
	auto blocks = nano::test::setup_chain (system, node, 1);
	auto const & block = blocks.front ();
	node.vote_generator.vote_normal (block->qualified_root (), block->hash (), 0);

	// Only the real block should produce a vote
	ASSERT_TIMELY_EQ (5s, votes->size (), 1);
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_generator, nano::stat::detail::broadcast), 1);

	auto locked = votes.lock ();
	ASSERT_EQ (1, locked->at (0)->hashes.size ());
	ASSERT_EQ (block->hash (), locked->at (0)->hashes[0]);

	// The fake hash should never appear in any vote
	for (auto const & vote : locked.get ())
	{
		for (auto const & hash : vote->hashes)
		{
			ASSERT_NE (fake_hash, hash);
		}
	}
}
