#pragma once

#include <celerix/store/db_val.hpp>

#include <rocksdb/slice.h>

namespace celerix::store::rocksdb
{
using db_val = store::db_val<::rocksdb::Slice>;
}
