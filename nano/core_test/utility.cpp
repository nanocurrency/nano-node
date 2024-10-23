#include <nano/lib/optional_ptr.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/relaxed_atomic.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/pending_info.hpp>
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

TEST (filesystem, remove_all_files)
{
	auto path = nano::unique_path ();
	auto dummy_directory = path / "tmp";
	std::filesystem::create_directories (dummy_directory);

	auto dummy_file1 = path / "my_file1.txt";
	auto dummy_file2 = path / "my_file2.txt";
	std::ofstream (dummy_file1.string ());
	std::ofstream (dummy_file2.string ());

	// Check all exist
	ASSERT_TRUE (std::filesystem::exists (dummy_directory));
	ASSERT_TRUE (std::filesystem::exists (dummy_file1));
	ASSERT_TRUE (std::filesystem::exists (dummy_file2));

	// Should remove only the files
	nano::remove_all_files_in_dir (path);

	ASSERT_TRUE (std::filesystem::exists (dummy_directory));
	ASSERT_FALSE (std::filesystem::exists (dummy_file1));
	ASSERT_FALSE (std::filesystem::exists (dummy_file2));
}

TEST (filesystem, move_all_files)
{
	auto path = nano::unique_path ();
	auto dummy_directory = path / "tmp";
	std::filesystem::create_directories (dummy_directory);

	auto dummy_file1 = dummy_directory / "my_file1.txt";
	auto dummy_file2 = dummy_directory / "my_file2.txt";
	std::ofstream (dummy_file1.string ());
	std::ofstream (dummy_file2.string ());

	// Check all exist
	ASSERT_TRUE (std::filesystem::exists (dummy_directory));
	ASSERT_TRUE (std::filesystem::exists (dummy_file1));
	ASSERT_TRUE (std::filesystem::exists (dummy_file2));

	// Should move only the files
	nano::move_all_files_to_dir (dummy_directory, path);

	ASSERT_TRUE (std::filesystem::exists (dummy_directory));
	ASSERT_TRUE (std::filesystem::exists (path / "my_file1.txt"));
	ASSERT_TRUE (std::filesystem::exists (path / "my_file2.txt"));
	ASSERT_FALSE (std::filesystem::exists (dummy_file1));
	ASSERT_FALSE (std::filesystem::exists (dummy_file2));
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

TEST (pending_key, sorting)
{
	nano::pending_key one{ 1, 2 };
	nano::pending_key two{ 1, 3 };
	nano::pending_key three{ 2, 1 };
	ASSERT_LT (one, two);
	ASSERT_LT (one, three);
	ASSERT_LT (two, three);
	nano::pending_key one_same{ 1, 2 };
	ASSERT_EQ (std::hash<nano::pending_key>{}(one), std::hash<nano::pending_key>{}(one_same));
	ASSERT_NE (std::hash<nano::pending_key>{}(one), std::hash<nano::pending_key>{}(two));
}
