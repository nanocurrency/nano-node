#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/secure/common.hpp>

struct MDB_val;

namespace nano
{
class pending_info_v14 final
{
public:
	pending_info_v14 () = default;
	pending_info_v14 (nano::account const &, nano::amount const &, nano::epoch);
	size_t db_size () const;
	bool deserialize (nano::stream &);
	bool operator== (nano::pending_info_v14 const &) const;
	nano::account source{ 0 };
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
	nano::account account{ 0 };
	nano::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
};
class state_block_w_sideband_v14
{
public:
	std::shared_ptr<nano::state_block> state_block;
	nano::block_sideband_v14 sideband;
};
class block_details_v18
{
	static_assert (std::is_same<std::underlying_type<nano::epoch>::type, uint8_t> (), "Epoch enum is not the proper type");
	static_assert (static_cast<uint8_t> (nano::epoch::max) < (1 << 5), "Epoch max is too large for the sideband");

public:
	block_details_v18 () = default;
	block_details_v18 (nano::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a);
	static constexpr size_t size ();
	bool operator== (block_details_v18 const & other_a) const;
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	nano::epoch epoch{ nano::epoch::epoch_0 };
	bool is_send{ false };
	bool is_receive{ false };
	bool is_epoch{ false };

private:
	uint8_t packed () const;
	void unpack (uint8_t);
};
class block_sideband_v18 final
{
public:
	block_sideband_v18 () = default;
	block_sideband_v18 (nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t, nano::block_details_v18 const &);
	block_sideband_v18 (nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t, nano::epoch, bool is_send, bool is_receive, bool is_epoch);
	void serialize (nano::stream &, nano::block_type) const;
	bool deserialize (nano::stream &, nano::block_type);
	static size_t size (nano::block_type);
	nano::block_hash successor{ 0 };
	nano::account account{ 0 };
	nano::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	nano::block_details_v18 details;
};
}
