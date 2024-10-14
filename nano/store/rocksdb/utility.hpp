#pragma once

#include <variant>

#include <rocksdb/utilities/transaction_db.h>

namespace nano::store
{
class transaction;
}

namespace nano::store::rocksdb
{
auto tx (store::transaction const & transaction_a) -> std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *>;
}
