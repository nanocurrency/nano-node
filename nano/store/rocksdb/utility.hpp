#pragma once

#include <nano/store/rocksdb/db_val.hpp>
#include <nano/store/tables.hpp>

#include <rocksdb/utilities/transaction_db.h>

namespace nano::store
{
class transaction;
class write_transaction;
}

namespace nano::store::rocksdb
{
auto is_read (nano::store::transaction const & transaction_a) -> bool;
auto tx (store::transaction const & transaction_a) -> ::rocksdb::Transaction *;
}
