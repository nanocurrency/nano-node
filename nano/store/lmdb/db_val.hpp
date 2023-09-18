#pragma once

#include <nano/store/db_val.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
using mdb_val = db_val<MDB_val>;
}
