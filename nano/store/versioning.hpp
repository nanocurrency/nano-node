#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/secure/common.hpp>

struct MDB_val;

namespace nano::store
{
class pending_info_v14 final
{
public:
	pending_info_v14 () = default;
	pending_info_v14 (nano::account const &, nano::amount const &, nano::epoch);
	size_t db_size () const;
	bool deserialize (nano::stream &);
	bool operator== (pending_info_v14 const &) const;
	nano::account source{};
	nano::amount amount{ 0 };
	nano::epoch epoch{ nano::epoch::epoch_0 };
};
class account_info_v14 final
{
public:
	account_info_v14 () = default;
	account_info_v14 (nano::block_hash const &, nano::block_hash const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t, uint64_t, nano::epoch);
	size_t db_size () const;
	nano::block_hash head{ 0 };
	nano::block_hash rep_block{ 0 };
	nano::block_hash open_block{ 0 };
	nano::amount balance{ 0 };
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	uint64_t confirmation_height{ 0 };
	nano::epoch epoch{ nano::epoch::epoch_0 };
};
class block_sideband_v14 final
{
public:
	block_sideband_v14 () = default;
	block_sideband_v14 (nano::block_type, nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	static size_t size (nano::block_type);
	nano::block_type type{ nano::block_type::invalid };
	nano::block_hash successor{ 0 };
	nano::account account{};
	nano::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
};
class state_block_w_sideband_v14
{
public:
	std::shared_ptr<nano::state_block> state_block;
	block_sideband_v14 sideband;
};
class block_sideband_v18 final
{
public:
	block_sideband_v18 () = default;
	block_sideband_v18 (nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t, nano::block_details const &);
	block_sideband_v18 (nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t, nano::epoch, bool is_send, bool is_receive, bool is_epoch);
	void serialize (nano::stream &, nano::block_type) const;
	bool deserialize (nano::stream &, nano::block_type);
	static size_t size (nano::block_type);
	nano::block_hash successor{ 0 };
	nano::account account{};
	nano::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	nano::block_details details;
};
// Move to versioning with a specific version if required for a future upgrade
template <typename T>
class block_w_sideband_v18
{
public:
	std::shared_ptr<T> block;
	block_sideband_v18 sideband;
};
} // namespace nano::store
