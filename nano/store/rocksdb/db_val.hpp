#pragma once

#include <nano/store/db_val.hpp>

#include <rocksdb/slice.h>

namespace nano::store::rocksdb
{
using db_val = store::db_val<::rocksdb::Slice>;
}
