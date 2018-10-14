#pragma once

#include <rai/lib/blocks.hpp>
#include <rai/node/lmdb.hpp>
#include <rai/secure/utility.hpp>

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
class pending_info_v3
{
public:
	pending_info_v3 ();
	pending_info_v3 (MDB_val const &);
	pending_info_v3 (rai::account const &, rai::amount const &, rai::account const &);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::pending_info_v3 const &) const;
	rai::mdb_val val () const;
	rai::account source;
	rai::amount amount;
	rai::account destination;
};
class pending_info_v4
{
public:
	pending_info_v4 ();
	pending_info_v4 (rai::account const &, rai::amount const &, epoch);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::pending_info_v4 const &) const;
	rai::account source;
	rai::amount amount;
	rai::epoch epoch;
};
// Latest information about an account
class account_info_v5
{
public:
	account_info_v5 ();
	account_info_v5 (MDB_val const &);
	account_info_v5 (rai::account_info_v5 const &) = default;
	account_info_v5 (rai::block_hash const &, rai::block_hash const &, rai::block_hash const &, rai::amount const &, uint64_t);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	rai::mdb_val val () const;
	rai::block_hash head;
	rai::block_hash rep_block;
	rai::block_hash open_block;
	rai::amount balance;
	uint64_t modified;
};
class account_info_v6
{
public:
	account_info_v6 ();
	account_info_v6 (rai::account_info_v6 const &) = default;
	account_info_v6 (rai::block_hash const &, rai::block_hash const &, rai::block_hash const &, rai::amount const &, uint64_t, uint64_t, epoch);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::account_info_v6 const &) const;
	bool operator!= (rai::account_info_v6 const &) const;
	size_t db_size () const;
	rai::block_hash head;
	rai::block_hash rep_block;
	rai::block_hash open_block;
	rai::amount balance;
	/** Seconds since posix epoch */
	uint64_t modified;
	uint64_t block_count;
	rai::epoch epoch;
};
}
