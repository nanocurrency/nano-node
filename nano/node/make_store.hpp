#pragma once

#include <nano/lib/diagnosticsconfig.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/rocksdbconfig.hpp>

#include <chrono>

namespace boost::filesystem
{
class path;
}

namespace nano
{
class ledger_constants;
class lmdb_config;
class rocksdb_config;
class txn_tracking_config;
}

namespace nano::store
{
class component;
}

namespace nano
{
std::unique_ptr<nano::store::component> make_store (nano::logger_mt & logger, boost::filesystem::path const & path, nano::ledger_constants & constants, bool open_read_only = false, bool add_db_postfix = true, nano::rocksdb_config const & rocksdb_config = nano::rocksdb_config{}, nano::txn_tracking_config const & txn_tracking_config_a = nano::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), nano::lmdb_config const & lmdb_config_a = nano::lmdb_config{}, bool backup_before_upgrade = false);
}
