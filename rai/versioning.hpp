#pragma once

#include <rai/utility.hpp>

namespace rai
{
class account_info_v1
{
public:
	account_info_v1 ();
	account_info_v1 (MDB_val const &);
	account_info_v1 (rai::account_info_v1 const &) = default;
	account_info_v1 (rai::block_hash const &, rai::block_hash const &, rai::amount const &, uint64_t);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	rai::mdb_val val () const;
	rai::block_hash head;
	rai::block_hash rep_block;
	rai::amount balance;
	uint64_t modified;
};
}