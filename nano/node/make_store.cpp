#include <nano/lib/logging.hpp>
#include <nano/node/make_store.hpp>
#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>

std::unique_ptr<nano::store::component> nano::make_store (nano::logger & logger, std::filesystem::path const & path, nano::ledger_constants & constants, bool read_only, bool add_db_postfix, nano::node_config const & node_config)
{
	if (node_config.database_backend == nano::database_backend::rocksdb)
	{
		return std::make_unique<nano::store::rocksdb::component> (logger, add_db_postfix ? path / "rocksdb" : path, constants, node_config.rocksdb_config, read_only);
	}
	else if (node_config.database_backend == nano::database_backend::lmdb)
	{
		return std::make_unique<nano::store::lmdb::component> (logger, add_db_postfix ? path / "data.ldb" : path, constants, node_config.diagnostics_config.txn_tracking, node_config.block_processor_batch_max_time, node_config.lmdb_config, node_config.backup_before_upgrade);
	}
	else if (node_config.database_backend == nano::database_backend::automatic)
	{
		bool lmdb_ledger_found = std::filesystem::exists (path / "data.ldb");
		bool rocks_ledger_found = std::filesystem::exists (path / "rocksdb");
		if (lmdb_ledger_found && rocks_ledger_found)
		{
			logger.warn (nano::log::type::ledger, "Multiple ledgers were found! Using RocksDb ledger");
			return std::make_unique<nano::store::rocksdb::component> (logger, add_db_postfix ? path / "rocksdb" : path, constants, node_config.rocksdb_config, read_only);
		}
		else if (lmdb_ledger_found)
		{
			logger.info (nano::log::type::ledger, "Using existing LMDB ledger");
			return std::make_unique<nano::store::lmdb::component> (logger, add_db_postfix ? path / "data.ldb" : path, constants, node_config.diagnostics_config.txn_tracking, node_config.block_processor_batch_max_time, node_config.lmdb_config, node_config.backup_before_upgrade);
		}
		else if (rocks_ledger_found)
		{
			logger.info (nano::log::type::ledger, "Using existing RocksDb ledger");
			return std::make_unique<nano::store::rocksdb::component> (logger, add_db_postfix ? path / "rocksdb" : path, constants, node_config.rocksdb_config, read_only);
		}
		else if (!lmdb_ledger_found && !rocks_ledger_found)
		{
			logger.info (nano::log::type::ledger, "No ledger found. Creating new LMDB ledger");
			return std::make_unique<nano::store::lmdb::component> (logger, add_db_postfix ? path / "data.ldb" : path, constants, node_config.diagnostics_config.txn_tracking, node_config.block_processor_batch_max_time, node_config.lmdb_config, node_config.backup_before_upgrade);
		}
	}
	debug_assert (false);
}
