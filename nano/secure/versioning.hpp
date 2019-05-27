#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/utility.hpp>

struct MDB_val;

namespace nano
{
class account_info_v1 final
{
public:
	account_info_v1 () = default;
	explicit account_info_v1 (MDB_val const &);
	account_info_v1 (nano::block_hash const &, nano::block_hash const &, nano::amount const &, uint64_t);
	nano::block_hash head{ 0 };
	nano::block_hash rep_block{ 0 };
	nano::amount balance{ 0 };
	uint64_t modified{ 0 };
};
class pending_info_v3 final
{
public:
	pending_info_v3 () = default;
	explicit pending_info_v3 (MDB_val const &);
	pending_info_v3 (nano::account const &, nano::amount const &, nano::account const &);
	nano::account source{ 0 };
	nano::amount amount{ 0 };
	nano::account destination{ 0 };
};
class account_info_v5 final
{
public:
	account_info_v5 () = default;
	explicit account_info_v5 (MDB_val const &);
	account_info_v5 (nano::block_hash const &, nano::block_hash const &, nano::block_hash const &, nano::amount const &, uint64_t);
	nano::block_hash head{ 0 };
	nano::block_hash rep_block{ 0 };
	nano::block_hash open_block{ 0 };
	nano::amount balance{ 0 };
	uint64_t modified{ 0 };
};
class account_info_v13 final
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
