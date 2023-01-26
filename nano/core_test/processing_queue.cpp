#include <nano/lib/processing_queue.hpp>
#include <nano/lib/stats.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (processing_queue, construction)
{
	nano::test::system system{};
	nano::processing_queue<int> queue{ system.stats, {}, {}, 4, 8 * 1024, 1024 };
	ASSERT_EQ (queue.size (), 0);
}

TEST (processing_queue, process_one)
{
	nano::test::system system{};
	nano::processing_queue<int> queue{ system.stats, {}, {}, 4, 8 * 1024, 1024 };

	std::atomic<std::size_t> processed{ 0 };
	queue.process_batch = [&] (auto & batch) {
		processed += batch.size ();
	};
	queue.start ();

	queue.add (1);

	ASSERT_TIMELY (5s, processed == 1);
	ASSERT_ALWAYS (1s, processed == 1);
	ASSERT_EQ (queue.size (), 0);

	queue.stop ();
}

TEST (processing_queue, process_many)
{
	nano::test::system system{};
	nano::processing_queue<int> queue{ system.stats, {}, {}, 4, 8 * 1024, 1024 };

	std::atomic<std::size_t> processed{ 0 };
	queue.process_batch = [&] (auto & batch) {
		processed += batch.size ();
	};
	queue.start ();

	const int count = 1024;
	for (int n = 0; n < count; ++n)
	{
		queue.add (1);
	}

	ASSERT_TIMELY (5s, processed == count);
	ASSERT_ALWAYS (1s, processed == count);
	ASSERT_EQ (queue.size (), 0);

	queue.stop ();
}

TEST (processing_queue, max_queue_size)
{
	nano::test::system system{};
	nano::processing_queue<int> queue{ system.stats, {}, {}, 4, 1024, 128 };

	const int count = 2 * 1024; // Double the max queue size
	for (int n = 0; n < count; ++n)
	{
		queue.add (1);
	}

	ASSERT_EQ (queue.size (), 1024);

	queue.stop ();
}

TEST (processing_queue, max_batch_size)
{
	nano::test::system system{};
	nano::processing_queue<int> queue{ system.stats, {}, {}, 4, 1024, 128 };

	// Fill queue before starting processing threads
	const int count = 1024;
	for (int n = 0; n < count; ++n)
	{
		queue.add (1);
	}

	std::atomic<std::size_t> max_batch{ 0 };
	queue.process_batch = [&] (auto & batch) {
		if (batch.size () > max_batch)
		{
			max_batch = batch.size ();
		}
	};
	queue.start ();

	ASSERT_TIMELY (5s, max_batch == 128);
	ASSERT_ALWAYS (1s, max_batch == 128);
	ASSERT_EQ (queue.size (), 0);

	queue.stop ();
}

TEST (processing_queue, parallel)
{
	nano::test::system system{};
	nano::processing_queue<int> queue{ system.stats, {}, {}, 16, 1024, 1 };

	std::atomic<std::size_t> processed{ 0 };
	queue.process_batch = [&] (auto & batch) {
		std::this_thread::sleep_for (2s);
		processed += batch.size ();
	};
	queue.start ();

	const int count = 16;
	for (int n = 0; n < count; ++n)
	{
		queue.add (1);
	}

	// There are 16 threads and 16 items, each thread is waiting 1 second inside processing callback
	// If processing is done in parallel it should take ~2 seconds to process every item, but keep some margin for slow machines
	ASSERT_TIMELY (3s, processed == count);
	ASSERT_EQ (queue.size (), 0);

	queue.stop ();
}