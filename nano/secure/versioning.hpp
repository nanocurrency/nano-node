#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/node/lmdb.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/utility.hpp>

namespace nano
{
class account_info_v1
{
public:
	account_info_v1 ();
	account_info_v1 (MDB_val const &);
	account_info_v1 (nano::account_info_v1 const &) = default;
	account_info_v1 (nano::block_hash const &, nano::block_hash const &, nano::amount const &, uint64_t);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	nano::mdb_val val () const;
	nano::block_hash head;
	nano::block_hash rep_block;
	nano::amount balance;
	uint64_t modified;
};
class pending_info_v3
{
public:
	pending_info_v3 ();
	pending_info_v3 (MDB_val const &);
	pending_info_v3 (nano::account const &, nano::amount const &, nano::account const &);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	bool operator== (nano::pending_info_v3 const &) const;
	nano::mdb_val val () const;
	nano::account source;
	nano::amount amount;
	nano::account destination;
};
class account_info_v5
{
public:
	account_info_v5 ();
	account_info_v5 (MDB_val const &);
	account_info_v5 (nano::account_info_v5 const &) = default;
	account_info_v5 (nano::block_hash const &, nano::block_hash const &, nano::block_hash const &, nano::amount const &, uint64_t);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	nano::mdb_val val () const;
	nano::block_hash head;
	nano::block_hash rep_block;
	nano::block_hash open_block;
	nano::amount balance;
	uint64_t modified;
};
class account_info_v13
{
public:
	account_info_v13 () = default;
	account_info_v13 (nano::block_hash const &, nano::block_hash const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t block_count, nano::epoch epoch_a);
	size_t db_size () const;
	nano::block_hash head{ 0 };
	nano::block_hash rep_block{ 0 };
	nano::block_hash open_block{ 0 };
	nano::amount balance{ 0 };
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	nano::epoch epoch{ nano::epoch::epoch_0 };
};
}
