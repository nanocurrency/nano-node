#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/node/lmdb.hpp>
#include <nano/secure/utility.hpp>

namespace nano
{
class account_info_v1
{
public:
	account_info_v1 () = default;
	account_info_v1 (MDB_val const &);
	account_info_v1 (nano::block_hash const &, nano::block_hash const &, nano::amount const &, uint64_t);
	nano::mdb_val val () const;
	nano::block_hash head{ 0 };
	nano::block_hash rep_block{ 0 };
	nano::amount balance{ 0 };
	uint64_t modified{ 0 };
};
class pending_info_v3
{
public:
	pending_info_v3 () = default;
	pending_info_v3 (MDB_val const &);
	pending_info_v3 (nano::account const &, nano::amount const &, nano::account const &);
	nano::mdb_val val () const;
	nano::account source{ 0 };
	nano::amount amount{ 0 };
	nano::account destination{ 0 };
};
// Latest information about an account
class account_info_v5
{
public:
	account_info_v5 () = default;
	account_info_v5 (MDB_val const &);
	account_info_v5 (nano::block_hash const &, nano::block_hash const &, nano::block_hash const &, nano::amount const &, uint64_t);
	nano::mdb_val val () const;
	nano::block_hash head{ 0 };
	nano::block_hash rep_block{ 0 };
	nano::block_hash open_block{ 0 };
	nano::amount balance{ 0 };
	uint64_t modified{ 0 };
};
}
