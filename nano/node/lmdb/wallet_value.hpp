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
	wallet_value (nano::uint256_union const &, nano::proof_of_work const &);
	nano::db_val<MDB_val> val () const;
	void serialize (nano::stream & stream_a) const;
	nano::uint256_union key;
	nano::proof_of_work work;

private:
	template <typename T>
	void deserialize_work (nano::db_val<MDB_val> const &);
};
}
