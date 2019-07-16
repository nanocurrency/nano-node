#include <nano/secure/utility.hpp>

#include <gtest/gtest.h>

#include <thread>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/utilities/transaction_db.h>

TEST (rocksdb, build_test)
{
	auto path = nano::unique_path ();

	std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
	column_families.push_back (rocksdb::ColumnFamilyDescriptor (
	rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions ()));

	rocksdb::Options options;
	options.create_if_missing = true;
	options.IncreaseParallelism (std::thread::hardware_concurrency ());
	options.OptimizeLevelStyleCompaction ();
	options.OptimizeUniversalStyleCompaction ();

	std::vector<rocksdb::ColumnFamilyHandle *> handles;
	rocksdb::OptimisticTransactionDB * db;
	auto s = rocksdb::OptimisticTransactionDB::Open (options, path.string (), column_families, &handles, &db);
	ASSERT_TRUE (s.ok ());
}
