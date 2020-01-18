#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/worker.hpp>
#include <nano/secure/utility.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

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
