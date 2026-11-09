#include <nano/lib/data_dir_lock.hpp>
#include <nano/lib/files.hpp>
#include <nano/node/inactive_node.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>

/*
 * One process at a time holds a data directory: a second lock on it is refused without waiting and
 * succeeds once the first is released, while another directory is not affected
 */
TEST (data_dir_lock, exclusive)
{
	auto const path = nano::unique_path ();
	std::filesystem::create_directories (path);

	// The first holder creates the lock file and takes the directory
	auto first = std::make_unique<nano::data_dir_lock> (path);
	ASSERT_TRUE (std::filesystem::exists (nano::data_dir_lock::lock_file (path)));

	// A second holder is refused while the first lives, another directory is free
	ASSERT_THROW (nano::data_dir_lock{ path }, std::runtime_error);
	auto const other = nano::unique_path ();
	std::filesystem::create_directories (other);
	ASSERT_NO_THROW (nano::data_dir_lock{ other });

	// Releasing the first holder frees the directory, the lock file stays behind
	first.reset ();
	ASSERT_NO_THROW (nano::data_dir_lock{ path });
	ASSERT_TRUE (std::filesystem::exists (nano::data_dir_lock::lock_file (path)));
}

/*
 * A node opened for writing takes the directory lock and a read only one does not, so maintenance
 * commands that write refuse to run next to a live node while read only ones still can
 */
TEST (data_dir_lock, inactive_node)
{
	// A data directory with a ledger, as a stopped node leaves it
	auto const path = nano::unique_path ();
	{
		auto flags = nano::inactive_node_flag_defaults ();
		flags.read_only = false;
		nano::inactive_node node{ path, flags };
	}

	// Another process holds the directory
	nano::data_dir_lock held{ path };

	// Opening for writing is refused, opening read only goes ahead
	auto writing = nano::inactive_node_flag_defaults ();
	writing.read_only = false;
	ASSERT_THROW (nano::inactive_node (path, writing), std::runtime_error);
	ASSERT_NO_THROW (nano::inactive_node (path, nano::inactive_node_flag_defaults ()));
}
