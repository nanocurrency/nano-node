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
	std::optional<nano::block_hash> successor (store::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
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
