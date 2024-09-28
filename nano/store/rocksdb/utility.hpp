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
auto count (store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a) -> uint64_t;
auto db (store::transaction const & transaction_a) -> ::rocksdb::DB *;
auto del (store::write_transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a) -> int;
auto exists (store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a) -> bool;
auto get (store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val & value_a) -> int;
auto is_read (nano::store::transaction const & transaction_a) -> bool;
auto iter (nano::store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a) -> ::rocksdb::Iterator *;
auto put (store::write_transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val const & value_a) -> int;
auto snapshot_options (nano::store::transaction const & transaction_a) -> ::rocksdb::ReadOptions &;
auto tx (store::transaction const & transaction_a) -> ::rocksdb::Transaction *;
}
