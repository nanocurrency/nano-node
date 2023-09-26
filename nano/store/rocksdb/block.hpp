#pragma once

#include <nano/store/block.hpp>
#include <nano/store/rocksdb/db_val.hpp>

namespace nano
{
class block_predecessor_rocksdb_set;
}
namespace nano::store::rocksdb
{
class component;
}
namespace nano::store::rocksdb
{
class block : public nano::store::block
{
	friend class nano::block_predecessor_rocksdb_set;
	nano::store::rocksdb::component & store;

public:
	explicit block (nano::store::rocksdb::component & store_a);
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

protected:
	void block_raw_get (store::transaction const & transaction_a, nano::block_hash const & hash_a, nano::store::rocksdb::db_val & value) const;
	size_t block_successor_offset (store::transaction const & transaction_a, size_t entry_size_a, nano::block_type type_a) const;
	static nano::block_type block_type_from_raw (void * data_a);
};
} // namespace nano::store::rocksdb
