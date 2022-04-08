#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
class mdb_store;
using mdb_val = db_val<MDB_val>;
class block_predecessor_mdb_set;
class block_store_mdb : public block_store
{
	friend class block_predecessor_mdb_set;
	nano::mdb_store & store;

public:
	explicit block_store_mdb (nano::mdb_store & store_a);
	void put (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a, nano::block const & block_a) override;
	void raw_put (nano::write_transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_hash const & hash_a) override;
	nano::block_hash successor (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	void successor_clear (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
	std::shared_ptr<nano::block> get (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	std::shared_ptr<nano::block> get_no_sideband (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	std::shared_ptr<nano::block> random (nano::transaction const & transaction_a) override;
	void del (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) override;
	bool exists (nano::transaction const & transaction_a, nano::block_hash const & hash_a) override;
	uint64_t count (nano::transaction const & transaction_a) override;
	nano::account account (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	nano::account account_calculated (nano::block const & block_a) const override;
	nano::store_iterator<nano::block_hash, nano::block_w_sideband> begin (nano::transaction const & transaction_a) const override;
	nano::store_iterator<nano::block_hash, nano::block_w_sideband> begin (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;
	nano::store_iterator<nano::block_hash, nano::block_w_sideband> end () const override;
	nano::uint128_t balance (nano::transaction const & transaction_a, nano::block_hash const & hash_a) override;
	nano::uint128_t balance_calculated (std::shared_ptr<nano::block> const & block_a) const override;
	nano::epoch version (nano::transaction const & transaction_a, nano::block_hash const & hash_a) override;
	void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, block_w_sideband>, nano::store_iterator<nano::block_hash, block_w_sideband>)> const & action_a) const override;
	// Converts a block hash to a block height
	uint64_t account_height (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;

protected:
	void block_raw_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::mdb_val & value) const;
	size_t block_successor_offset (nano::transaction const & transaction_a, size_t entry_size_a, nano::block_type type_a) const;
	static nano::block_type block_type_from_raw (void * data_a);
};
}
