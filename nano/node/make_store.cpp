#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/make_store.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/store/lmdb/backend_lmdb.hpp>
#include <nano/store/rocksdb/backend_rocksdb.hpp>

std::filesystem::path nano::database_path_for_backend (std::filesystem::path const & base_path, database_backend backend_type)
{
	switch (backend_type)
	{
		case nano::database_backend::lmdb:
			return base_path / "data.ldb";
		case nano::database_backend::rocksdb:
			return base_path / "rocksdb";
	}
	release_assert (false, "unknown database backend");
}

std::unique_ptr<nano::store::ledger_store> nano::make_store (nano::logger & logger, nano::stats & stats, std::filesystem::path const & path, nano::ledger_constants & constants, bool read_only, bool add_db_postfix, nano::node_config node_config)
{
	auto decide_backend = [&] () -> nano::database_backend {
		if (node_config.rocksdb_config.enable && node_config.database_backend == nano::database_backend::lmdb)
		{
			logger.warn (nano::log::type::config, "Use of deprecated `[node.rocksdb].enable` setting detected in config file, defaulting to RocksDB backend.\nPlease edit config-node.toml and use the new '[node].database_backend' for future compatibility.");
			return nano::database_backend::rocksdb;
		}
		return node_config.database_backend;
	};

	nano::store::open_mode const mode = read_only ? nano::store::open_mode::read_only : nano::store::open_mode::read_write;

	auto backend_type = decide_backend ();
	std::unique_ptr<nano::store::backend> backend;

	switch (backend_type)
	{
		case nano::database_backend::lmdb:
		{
			auto db_path = add_db_postfix ? database_path_for_backend (path, backend_type) : path;
			backend = std::make_unique<nano::store::lmdb::backend_lmdb> (db_path, node_config.lmdb_config, logger, node_config.txn_tracking);
			break;
		}
		case nano::database_backend::rocksdb:
		{
			auto db_path = add_db_postfix ? database_path_for_backend (path, backend_type) : path;
			backend = std::make_unique<nano::store::rocksdb::backend_rocksdb> (db_path, node_config.rocksdb_config, logger, node_config.txn_tracking);
			break;
		}
	}

	release_assert (backend != nullptr);

	nano::store::ledger_store_params params;
	params.backup_before_upgrade = node_config.backup_before_upgrade;

	return std::make_unique<nano::store::ledger_store> (std::move (backend), mode, stats, logger, params);
}
