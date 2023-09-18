#pragma once

#include <nano/store/block.hpp>
#include <nano/store/lmdb/db_val.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
class block_predecessor_mdb_set;
}
namespace nano::store::lmdb
{
class component;
}
namespace nano::store::lmdb
{
class block : public nano::store::block
{
	friend class block_predecessor_mdb_set;
	nano::store::lmdb::component & store;

public:
	explicit block (nano::store::lmdb::component & store_a);
	void put (store::write_transaction const & transaction_a, nano::block_hash const & hash_a, nano::block const & block_a) override;
	void raw_put (store::write_transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_hash const & hash_a) override;
	nano::block_hash successor (store::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	void successor_clear (store::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
	std::shared_ptr<nano::block> get (store::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	std::shared_ptr<nano::block> random (store::transaction const & transaction_a) override;
	void del (store::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
	bool exists (store::transaction const & transaction_a, nano::block_hash const & hash_a) override;
	uint64_t count (store::transaction const & transaction_a) override;
	store::iterator<nano::block_hash, nano::store::block_w_sideband> begin (store::transaction const & transaction_a) const override;
	store::iterator<nano::block_hash, nano::store::block_w_sideband> begin (store::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	store::iterator<nano::block_hash, nano::store::block_w_sideband> end () const override;
	void for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::block_hash, block_w_sideband>, store::iterator<nano::block_hash, block_w_sideband>)> const & action_a) const override;

	/**
		 * Maps block hash to send block. (Removed)
		 * nano::block_hash -> nano::send_block
		 */
	MDB_dbi send_blocks_handle{ 0 };

	/**
		 * Maps block hash to receive block. (Removed)
		 * nano::block_hash -> nano::receive_block
		 */
	MDB_dbi receive_blocks_handle{ 0 };

	/**
		 * Maps block hash to open block. (Removed)
		 * nano::block_hash -> nano::open_block
		 */
	MDB_dbi open_blocks_handle{ 0 };

	/**
		 * Maps block hash to change block. (Removed)
		 * nano::block_hash -> nano::change_block
		 */
	MDB_dbi change_blocks_handle{ 0 };

	/**
		 * Maps block hash to v0 state block. (Removed)
		 * nano::block_hash -> nano::state_block
		 */
	MDB_dbi state_blocks_v0_handle{ 0 };

	/**
		 * Maps block hash to v1 state block. (Removed)
		 * nano::block_hash -> nano::state_block
		 */
	MDB_dbi state_blocks_v1_handle{ 0 };

	/**
		 * Maps block hash to state block. (Removed)
		 * nano::block_hash -> nano::state_block
		 */
	MDB_dbi state_blocks_handle{ 0 };

	/**
		 * Meta information about block store, such as versions.
		 * nano::uint256_union (arbitrary key) -> blob
		 */
	MDB_dbi meta_handle{ 0 };

	/**
		 * Contains block_sideband and block for all block types (legacy send/change/open/receive & state blocks)
		 * nano::block_hash -> nano::block_sideband, nano::block
		 */
	MDB_dbi blocks_handle{ 0 };

protected:
	void block_raw_get (store::transaction const & transaction_a, nano::block_hash const & hash_a, db_val & value) const;
	size_t block_successor_offset (store::transaction const & transaction_a, size_t entry_size_a, nano::block_type type_a) const;
	static nano::block_type block_type_from_raw (void * data_a);
};
} // namespace nano::store::lmdb
