#pragma once

#include <nano/store/db_val.hpp>

#include <rocksdb/slice.h>

namespace nano
{
using rocksdb_val = db_val<::rocksdb::Slice>;
}
