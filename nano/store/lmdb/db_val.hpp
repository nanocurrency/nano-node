#pragma once

#include <nano/store/db_val.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
using db_val = store::db_val<MDB_val>;
}
