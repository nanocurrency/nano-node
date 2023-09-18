#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (nano::db_val<MDB_val> const &);
	wallet_value (nano::raw_key const &, uint64_t);
	nano::db_val<MDB_val> val () const;
	nano::raw_key key;
	uint64_t work;
};
}
