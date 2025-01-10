#pragma once

#include <celerix/lib/diagnosticsconfig.hpp>
#include <celerix/lib/lmdbconfig.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/rocksdbconfig.hpp>

#include <chrono>

namespace celerix
{
class ledger_constants;
class lmdb_config;
class rocksdb_config;
class txn_tracking_config;
}

namespace celerix::store
{
class component;
}

namespace celerix
{
std::unique_ptr<celerix::store::component> make_store (celerix::logger &, std::filesystem::path const & path, celerix::ledger_constants & constants, bool open_read_only = false, bool add_db_postfix = true, celerix::rocksdb_config const & rocksdb_config = celerix::rocksdb_config{}, celerix::txn_tracking_config const & txn_tracking_config_a = celerix::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), celerix::lmdb_config const & lmdb_config_a = celerix::lmdb_config{}, bool backup_before_upgrade = false);
}
