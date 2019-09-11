#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/blockstore.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (nano::db_val<MDB_val> const &);
	wallet_value (nano::uint256_union const &, uint64_t);
	nano::db_val<MDB_val> val () const;
	nano::uint256_union key;
	uint64_t work;
};
}
