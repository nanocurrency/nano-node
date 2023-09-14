#pragma once

#include <nano/secure/store.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
using mdb_val = db_val<MDB_val>;
class block_predecessor_mdb_set;
namespace lmdb
{
	class store;
	class block_store : public nano::block_store
	{
		friend class nano::block_predecessor_mdb_set;
		nano::lmdb::store & store;

	public:
		explicit block_store (nano::lmdb::store & store_a);
		void put (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a, nano::block const & block_a) override;
		void raw_put (nano::write_transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_hash const & hash_a) override;
		nano::block_hash successor (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
		void successor_clear (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
		std::shared_ptr<nano::block> get (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
		std::shared_ptr<nano::block> random (nano::transaction const & transaction_a) override;
		void del (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
		bool exists (nano::transaction const & transaction_a, nano::block_hash const & hash_a) override;
		uint64_t count (nano::transaction const & transaction_a) override;
		nano::store_iterator<nano::block_hash, nano::block_w_sideband> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::block_hash, nano::block_w_sideband> begin (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
		nano::store_iterator<nano::block_hash, nano::block_w_sideband> end () const override;
		void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, block_w_sideband>, nano::store_iterator<nano::block_hash, block_w_sideband>)> const & action_a) const override;

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
		void block_raw_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::mdb_val & value) const;
		size_t block_successor_offset (nano::transaction const & transaction_a, size_t entry_size_a, nano::block_type type_a) const;
		static nano::block_type block_type_from_raw (void * data_a);
	};
}
}
