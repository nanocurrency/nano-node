#pragma once

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::lmdb
{
class unchecked_store
{
public:
	/**
	 * Unchecked bootstrap blocks info.
	 * nano::block_hash -> nano::unchecked_info
	 */
	MDB_dbi unchecked_handle{ 0 };
};
}
