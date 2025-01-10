#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/store/db_val.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix
{
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (store::db_val<MDB_val> const &);
	wallet_value (celerix::raw_key const &, uint64_t);
	store::db_val<MDB_val> val () const;
	celerix::raw_key key;
	uint64_t work;
};
}
