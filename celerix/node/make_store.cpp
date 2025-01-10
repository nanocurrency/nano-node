#include <celerix/lib/logging.hpp>
#include <celerix/node/make_store.hpp>
#include <celerix/store/lmdb/lmdb.hpp>
#include <celerix/store/rocksdb/rocksdb.hpp>

std::unique_ptr<celerix::store::component> celerix::make_store (celerix::logger & logger, std::filesystem::path const & path, celerix::ledger_constants & constants, bool read_only, bool add_db_postfix, celerix::rocksdb_config const & rocksdb_config, celerix::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, celerix::lmdb_config const & lmdb_config_a, bool backup_before_upgrade)
{
	if (rocksdb_config.enable)
	{
		return std::make_unique<celerix::store::rocksdb::component> (logger, add_db_postfix ? path / "rocksdb" : path, constants, rocksdb_config, read_only);
	}

	return std::make_unique<celerix::store::lmdb::component> (logger, add_db_postfix ? path / "data.ldb" : path, constants, txn_tracking_config_a, block_processor_batch_max_time_a, lmdb_config_a, backup_before_upgrade);
}
