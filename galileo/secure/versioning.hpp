#pragma once

#include <galileo/lib/blocks.hpp>
#include <galileo/node/lmdb.hpp>
#include <galileo/secure/utility.hpp>

namespace galileo
{
class account_info_v1
{
public:
	account_info_v1 ();
	account_info_v1 (MDB_val const &);
	account_info_v1 (galileo::account_info_v1 const &) = default;
	account_info_v1 (galileo::block_hash const &, galileo::block_hash const &, galileo::amount const &, uint64_t);
	void serialize (galileo::stream &) const;
	bool deserialize (galileo::stream &);
	galileo::mdb_val val () const;
	galileo::block_hash head;
	galileo::block_hash rep_block;
	galileo::amount balance;
	uint64_t modified;
};
class pending_info_v3
{
public:
	pending_info_v3 ();
	pending_info_v3 (MDB_val const &);
	pending_info_v3 (galileo::account const &, galileo::amount const &, galileo::account const &);
	void serialize (galileo::stream &) const;
	bool deserialize (galileo::stream &);
	bool operator== (galileo::pending_info_v3 const &) const;
	galileo::mdb_val val () const;
	galileo::account source;
	galileo::amount amount;
	galileo::account destination;
};
// Latest information about an account
class account_info_v5
{
public:
	account_info_v5 ();
	account_info_v5 (MDB_val const &);
	account_info_v5 (galileo::account_info_v5 const &) = default;
	account_info_v5 (galileo::block_hash const &, galileo::block_hash const &, galileo::block_hash const &, galileo::amount const &, uint64_t);
	void serialize (galileo::stream &) const;
	bool deserialize (galileo::stream &);
	galileo::mdb_val val () const;
	galileo::block_hash head;
	galileo::block_hash rep_block;
	galileo::block_hash open_block;
	galileo::amount balance;
	uint64_t modified;
};
}
