#pragma once

#include <nano/lib/assert.hpp>
#include <nano/store/db_val.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

// TODO: Move to common.hpp
namespace nano::store::lmdb
{
/**
 * Converts a db_val to MDB_val for LMDB operations
 */
inline MDB_val to_mdb_val (nano::store::db_val const & val)
{
	// Empty value needs valid pointer to avoid UB in LMDB's memcpy
	if (val.data () == nullptr || val.size () == 0)
	{
		debug_assert (val.size () == 0);
		return MDB_val{ 0, const_cast<void *> (static_cast<void const *> ("")) };
	}
	return MDB_val{ val.size (), val.data () };
}

/**
 * Creates a db_val from MDB_val for read operations
 */
inline nano::store::db_val from_mdb_val (MDB_val const & val)
{
	auto span = std::span<uint8_t const>{ static_cast<uint8_t const *> (val.mv_data), val.mv_size };
	return nano::store::db_val{ span };
}
}