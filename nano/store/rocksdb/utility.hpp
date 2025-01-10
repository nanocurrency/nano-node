#pragma once

#include <variant>

#include <rocksdb/utilities/transaction_db.h>

namespace celerix::store
{
class transaction;
}

namespace celerix::store::rocksdb
{
auto tx (store::transaction const & transaction_a) -> std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *>;
}
