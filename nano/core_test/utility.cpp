#include <nano/lib/optional_ptr.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/worker.hpp>
#include <nano/secure/utility.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

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
	// 5 mb/s long term rate, allow for 10 mb/s bursts
	nano::rate::token_bucket bucket (10, 5);

	// Initial burst of 10 mb/s
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

TEST (rate, unlimited)
{
	nano::rate::token_bucket bucket (0, 0);
	ASSERT_TRUE (bucket.try_consume (5));
	ASSERT_EQ (bucket.largest_burst (), 5);
	ASSERT_TRUE (bucket.try_consume (1e9));
	ASSERT_EQ (bucket.largest_burst (), 1e9);

	// With unlimited tokens, consuming always succeed
	ASSERT_TRUE (bucket.try_consume (1e9));
	ASSERT_EQ (bucket.largest_burst (), 1e9);
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

TEST (thread, worker)
{
	std::atomic<bool> passed_sleep{ false };

	auto func = [&passed_sleep]() {
		std::this_thread::sleep_for (std::chrono::seconds (1));
		passed_sleep = true;
	};

	nano::worker worker;
	worker.push_task (func);
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
