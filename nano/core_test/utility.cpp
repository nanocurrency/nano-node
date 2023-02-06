#include <nano/lib/optional_ptr.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/utility.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include <fstream>
#include <future>

using namespace std::chrono_literals;

TEST (rate, basic)
{
	nano::rate::token_bucket bucket (10, 10);

	// Initial burst
	ASSERT_TRUE (bucket.try_consume (10));
	ASSERT_FALSE (bucket.try_consume (10));

	// With a fill rate of 10 tokens/sec, await 1/3 sec and get 3 tokens
	std::this_thread::sleep_for (300ms);
	ASSERT_TRUE (bucket.try_consume (3));
	ASSERT_FALSE (bucket.try_consume (10));

	// Allow time for the bucket to completely refill and do a full burst
	std::this_thread::sleep_for (1s);
	ASSERT_TRUE (bucket.try_consume (10));
	ASSERT_EQ (bucket.largest_burst (), 10);
}

TEST (rate, network)
{
	// For the purpose of the test, one token represents 1MB instead of one byte.
	// Allow for 10 mb/s bursts (max bucket size), 5 mb/s long term rate
	nano::rate::token_bucket bucket (10, 5);

	// Initial burst of 10 mb/s over two calls
	ASSERT_TRUE (bucket.try_consume (5));
	ASSERT_EQ (bucket.largest_burst (), 5);
	ASSERT_TRUE (bucket.try_consume (5));
	ASSERT_EQ (bucket.largest_burst (), 10);
	ASSERT_FALSE (bucket.try_consume (5));

	// After 200 ms, the 5 mb/s fillrate means we have 1 mb available
	std::this_thread::sleep_for (200ms);
	ASSERT_TRUE (bucket.try_consume (1));
	ASSERT_FALSE (bucket.try_consume (1));
}

TEST (rate, reset)
{
	nano::rate::token_bucket bucket (0, 0);

	// consume lots of tokens, buckets should be unlimited
	ASSERT_TRUE (bucket.try_consume (1000000));
	ASSERT_TRUE (bucket.try_consume (1000000));

	// set bucket to be limited
	bucket.reset (1000, 1000);
	ASSERT_FALSE (bucket.try_consume (1001));
	ASSERT_TRUE (bucket.try_consume (1000));
	ASSERT_FALSE (bucket.try_consume (1000));
	std::this_thread::sleep_for (2ms);
	ASSERT_TRUE (bucket.try_consume (2));

	// reduce the limit
	bucket.reset (100, 100 * 1000);
	ASSERT_FALSE (bucket.try_consume (101));
	ASSERT_TRUE (bucket.try_consume (100));
	std::this_thread::sleep_for (1ms);
	ASSERT_TRUE (bucket.try_consume (100));

	// increase the limit
	bucket.reset (2000, 1);
	ASSERT_FALSE (bucket.try_consume (2001));
	ASSERT_TRUE (bucket.try_consume (2000));

	// back to unlimited
	bucket.reset (0, 0);
	ASSERT_TRUE (bucket.try_consume (1000000));
	ASSERT_TRUE (bucket.try_consume (1000000));
}

TEST (rate, unlimited)
{
	nano::rate::token_bucket bucket (0, 0);
	ASSERT_TRUE (bucket.try_consume (5));
	ASSERT_EQ (bucket.largest_burst (), 5);
	ASSERT_TRUE (bucket.try_consume (static_cast<size_t> (1e9)));
	ASSERT_EQ (bucket.largest_burst (), static_cast<size_t> (1e9));

	// With unlimited tokens, consuming always succeed
	ASSERT_TRUE (bucket.try_consume (static_cast<size_t> (1e9)));
	ASSERT_EQ (bucket.largest_burst (), static_cast<size_t> (1e9));
}

TEST (rate, busy_spin)
{
	// Bucket should refill at a rate of 1 token per second
	nano::rate::token_bucket bucket (1, 1);

	// Run a very tight loop for 5 seconds + a bit of wiggle room
	int counter = 0;
	for (auto start = std::chrono::steady_clock::now (), now = start; now < start + std::chrono::milliseconds{ 5500 }; now = std::chrono::steady_clock::now ())
	{
		if (bucket.try_consume ())
		{
			++counter;
		}
	}

	// Bucket starts fully refilled, therefore we see 1 additional request
	ASSERT_EQ (counter, 6);
}

TEST (optional_ptr, basic)
{
	struct valtype
	{
		int64_t x{ 1 };
		int64_t y{ 2 };
		int64_t z{ 3 };
	};

	nano::optional_ptr<valtype> opt;
	ASSERT_FALSE (opt);
	ASSERT_FALSE (opt.is_initialized ());

	{
		auto val = valtype{};
		opt = val;
		ASSERT_LT (sizeof (opt), sizeof (val));
		std::unique_ptr<valtype> uptr;
		ASSERT_EQ (sizeof (opt), sizeof (uptr));
	}
	ASSERT_TRUE (opt);
	ASSERT_TRUE (opt.is_initialized ());
	ASSERT_EQ (opt->x, 1);
	ASSERT_EQ (opt->y, 2);
	ASSERT_EQ (opt->z, 3);
}

TEST (thread, thread_pool)
{
	std::atomic<bool> passed_sleep{ false };

	auto func = [&passed_sleep] () {
		std::this_thread::sleep_for (std::chrono::seconds (1));
		passed_sleep = true;
	};

	nano::thread_pool workers (1u, nano::thread_role::name::unknown);
	workers.push_task (func);
	ASSERT_FALSE (passed_sleep);

	nano::timer<std::chrono::milliseconds> timer_l;
	timer_l.start ();
	while (!passed_sleep)
	{
		if (timer_l.since_start () > std::chrono::seconds (10))
		{
			break;
		}
	}
	ASSERT_TRUE (passed_sleep);
}

TEST (thread_pool_alarm, one)
{
	nano::thread_pool workers (1u, nano::thread_role::name::unknown);
	std::atomic<bool> done (false);
	nano::mutex mutex;
	nano::condition_variable condition;
	workers.add_timed_task (std::chrono::steady_clock::now (), [&] () {
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			done = true;
		}
		condition.notify_one ();
	});
	nano::unique_lock<nano::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return !!done; });
}

TEST (thread_pool_alarm, many)
{
	nano::thread_pool workers (50u, nano::thread_role::name::unknown);
	std::atomic<int> count (0);
	nano::mutex mutex;
	nano::condition_variable condition;
	for (auto i (0); i < 50; ++i)
	{
		workers.add_timed_task (std::chrono::steady_clock::now (), [&] () {
			{
				nano::lock_guard<nano::mutex> lock{ mutex };
				count += 1;
			}
			condition.notify_one ();
		});
	}
	nano::unique_lock<nano::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return count == 50; });
}

TEST (thread_pool_alarm, top_execution)
{
	nano::thread_pool workers (1u, nano::thread_role::name::unknown);
	int value1 (0);
	int value2 (0);
	nano::mutex mutex;
	std::promise<bool> promise;
	workers.add_timed_task (std::chrono::steady_clock::now (), [&] () {
		nano::lock_guard<nano::mutex> lock{ mutex };
		value1 = 1;
		value2 = 1;
	});
	workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::milliseconds (1), [&] () {
		nano::lock_guard<nano::mutex> lock{ mutex };
		value2 = 2;
		promise.set_value (false);
	});
	promise.get_future ().get ();
	nano::lock_guard<nano::mutex> lock{ mutex };
	ASSERT_EQ (1, value1);
	ASSERT_EQ (2, value2);
}

TEST (filesystem, remove_all_files)
{
	auto path = nano::unique_path ();
	auto dummy_directory = path / "tmp";
	boost::filesystem::create_directories (dummy_directory);

	auto dummy_file1 = path / "my_file1.txt";
	auto dummy_file2 = path / "my_file2.txt";
	std::ofstream (dummy_file1.string ());
	std::ofstream (dummy_file2.string ());

	// Check all exist
	ASSERT_TRUE (boost::filesystem::exists (dummy_directory));
	ASSERT_TRUE (boost::filesystem::exists (dummy_file1));
	ASSERT_TRUE (boost::filesystem::exists (dummy_file2));

	// Should remove only the files
	nano::remove_all_files_in_dir (path);

	ASSERT_TRUE (boost::filesystem::exists (dummy_directory));
	ASSERT_FALSE (boost::filesystem::exists (dummy_file1));
	ASSERT_FALSE (boost::filesystem::exists (dummy_file2));
}

TEST (filesystem, move_all_files)
{
	auto path = nano::unique_path ();
	auto dummy_directory = path / "tmp";
	boost::filesystem::create_directories (dummy_directory);

	auto dummy_file1 = dummy_directory / "my_file1.txt";
	auto dummy_file2 = dummy_directory / "my_file2.txt";
	std::ofstream (dummy_file1.string ());
	std::ofstream (dummy_file2.string ());

	// Check all exist
	ASSERT_TRUE (boost::filesystem::exists (dummy_directory));
	ASSERT_TRUE (boost::filesystem::exists (dummy_file1));
	ASSERT_TRUE (boost::filesystem::exists (dummy_file2));

	// Should move only the files
	nano::move_all_files_to_dir (dummy_directory, path);

	ASSERT_TRUE (boost::filesystem::exists (dummy_directory));
	ASSERT_TRUE (boost::filesystem::exists (path / "my_file1.txt"));
	ASSERT_TRUE (boost::filesystem::exists (path / "my_file2.txt"));
	ASSERT_FALSE (boost::filesystem::exists (dummy_file1));
	ASSERT_FALSE (boost::filesystem::exists (dummy_file2));
}

TEST (relaxed_atomic_integral, basic)
{
	nano::relaxed_atomic_integral<uint32_t> atomic{ 0 };
	ASSERT_EQ (0, atomic++);
	ASSERT_EQ (1, atomic);
	ASSERT_EQ (2, ++atomic);
	ASSERT_EQ (2, atomic);
	ASSERT_EQ (2, atomic.load ());
	ASSERT_EQ (2, atomic--);
	ASSERT_EQ (1, atomic);
	ASSERT_EQ (0, --atomic);
	ASSERT_EQ (0, atomic);
	ASSERT_EQ (0, atomic.fetch_add (2));
	ASSERT_EQ (2, atomic);
	ASSERT_EQ (2, atomic.fetch_sub (1));
	ASSERT_EQ (1, atomic);
	atomic.store (3);
	ASSERT_EQ (3, atomic);

	uint32_t expected{ 2 };
	ASSERT_FALSE (atomic.compare_exchange_strong (expected, 1));
	ASSERT_EQ (3, expected);
	ASSERT_EQ (3, atomic);
	ASSERT_TRUE (atomic.compare_exchange_strong (expected, 1));
	ASSERT_EQ (1, atomic);
	ASSERT_EQ (3, expected);

	// Weak can fail spuriously, try a few times
	bool res{ false };
	for (int i = 0; i < 1000; ++i)
	{
		res |= atomic.compare_exchange_weak (expected, 2);
		expected = 1;
	}
	ASSERT_TRUE (res);
	ASSERT_EQ (2, atomic);
}

TEST (relaxed_atomic_integral, many_threads)
{
	std::vector<std::thread> threads;
	auto num = 4;
	nano::relaxed_atomic_integral<uint32_t> atomic{ 0 };
	for (int i = 0; i < num; ++i)
	{
		threads.emplace_back ([&atomic] {
			for (int i = 0; i < 10000; ++i)
			{
				++atomic;
				atomic--;
				atomic++;
				--atomic;
				atomic.fetch_add (2);
				atomic.fetch_sub (2);
			}
		});
	}

	for (auto & thread : threads)
	{
		thread.join ();
	}

	// Check values
	ASSERT_EQ (0, atomic);
}
