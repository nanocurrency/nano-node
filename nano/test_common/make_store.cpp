#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/store/lmdb/backend_lmdb.hpp>
#include <nano/store/rocksdb/backend_rocksdb.hpp>
#include <nano/test_common/common.hpp>
#include <nano/test_common/make_store.hpp>

std::unique_ptr<nano::store::backend> nano::test::make_backend (std::filesystem::path path)
{
	path = path.empty () ? nano::unique_path () : path;

	auto backend_type = nano::default_database_backend ();
	switch (backend_type)
	{
		case nano::database_backend::lmdb:
		{
			auto db_path = path / "data.ldb";
			nano::lmdb_config lmdb_config{};
			return std::make_unique<nano::store::lmdb::backend_lmdb> (db_path, lmdb_config, nano::test::default_logger ());
		}
		case nano::database_backend::rocksdb:
		{
			auto db_path = path / "rocksdb";
			nano::rocksdb_config rocksdb_config{};
			return std::make_unique<nano::store::rocksdb::backend_rocksdb> (db_path, rocksdb_config, nano::test::default_logger ());
		}
	}
	release_assert (false, "unknown database backend");
}

std::unique_ptr<nano::store::ledger_store> nano::test::make_store (std::filesystem::path path)
{
	path = path.empty () ? nano::unique_path () : path;

	return nano::make_store (nano::test::default_logger (), nano::test::default_stats (), path, nano::dev::constants);
}
