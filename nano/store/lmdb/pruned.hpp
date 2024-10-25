#pragma once

#include <nano/store/pruned.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
class pruned : public nano::store::pruned
{
private:
	nano::store::lmdb::component & store;

public:
	explicit pruned (nano::store::lmdb::component & store_a);
	void put (store::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
	void del (store::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
	bool exists (store::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	nano::block_hash random (store::transaction const & transaction_a) override;
	size_t count (store::transaction const & transaction_a) const override;
	void clear (store::write_transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;

	/**
	 * Pruned blocks hashes
	 * nano::block_hash -> none
	 */
	MDB_dbi pruned_handle{ 0 };
};
} // namespace nano::store::lmdb
