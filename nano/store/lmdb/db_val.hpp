#pragma once

#include <celerix/store/db_val.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix::store::lmdb
{
using db_val = store::db_val<MDB_val>;
}
