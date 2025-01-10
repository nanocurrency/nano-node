#include <celerix/lib/processing_queue.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (processing_queue, construction)
{
	celerix::test::system system{};
	celerix::processing_queue<int> queue{ system.stats, celerix::stat::type::test, {}, 4, 8 * 1024, 1024 };
	ASSERT_EQ (queue.size (), 0);
}

TEST (processing_queue, process_one)
{
	celerix::test::system system{};
	celerix::processing_queue<int> queue{ system.stats, celerix::stat::type::test, {}, 4, 8 * 1024, 1024 };

	std::atomic<std::size_t> processed{ 0 };
	queue.process_batch = [&] (auto & batch) {
		processed += batch.size ();
	};
	celerix::test::start_stop_guard queue_guard{ queue };

	queue.add (1);

	ASSERT_TIMELY_EQ (5s, processed, 1);
	ASSERT_ALWAYS (1s, processed == 1);
	ASSERT_EQ (queue.size (), 0);
}

TEST (processing_queue, process_many)
{
	celerix::test::system system{};
	celerix::processing_queue<int> queue{ system.stats, celerix::stat::type::test, {}, 4, 8 * 1024, 1024 };

	std::atomic<std::size_t> processed{ 0 };
	queue.process_batch = [&] (auto & batch) {
		processed += batch.size ();
	};
	celerix::test::start_stop_guard queue_guard{ queue };

	const int count = 1024;
	for (int n = 0; n < count; ++n)
	{
		queue.add (1);
	}

	ASSERT_TIMELY_EQ (5s, processed, count);
	ASSERT_ALWAYS (1s, processed == count);
	ASSERT_EQ (queue.size (), 0);
}

TEST (processing_queue, max_queue_size)
{
	celerix::test::system system{};
	celerix::processing_queue<int> queue{ system.stats, celerix::stat::type::test, {}, 4, 1024, 128 };

	const int count = 2 * 1024; // Double the max queue size
	for (int n = 0; n < count; ++n)
	{
		queue.add (1);
	}

	ASSERT_EQ (queue.size (), 1024);
}

TEST (processing_queue, max_batch_size)
{
	celerix::test::system system{};
	celerix::processing_queue<int> queue{ system.stats, celerix::stat::type::test, {}, 4, 1024, 128 };

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
	celerix::test::start_stop_guard queue_guard{ queue };

	ASSERT_TIMELY_EQ (5s, max_batch, 128);
	ASSERT_ALWAYS (1s, max_batch == 128);
	ASSERT_EQ (queue.size (), 0);
}

TEST (processing_queue, parallel)
{
	celerix::test::system system{};
	celerix::processing_queue<int> queue{ system.stats, celerix::stat::type::test, {}, 16, 1024, 1 };

	std::atomic<std::size_t> processed{ 0 };
	queue.process_batch = [&] (auto & batch) {
		std::this_thread::sleep_for (2s);
		processed += batch.size ();
	};
	celerix::test::start_stop_guard queue_guard{ queue };

	const int count = 16;
	for (int n = 0; n < count; ++n)
	{
		queue.add (1);
	}

	// There are 16 threads and 16 items, each thread is waiting 1 second inside processing callback
	// If processing is done in parallel it should take ~2 seconds to process every item, but keep some margin for slow machines
	ASSERT_TIMELY_EQ (3s, processed, count);
	ASSERT_EQ (queue.size (), 0);
}
