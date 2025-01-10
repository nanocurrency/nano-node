#pragma once

#include <celerix/store/block.hpp>
#include <celerix/store/lmdb/db_val.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix::store::lmdb
{
class block_predecessor_mdb_set;
}
namespace celerix::store::lmdb
{
class component;
}
namespace celerix::store::lmdb
{
class block : public celerix::store::block
{
	friend class block_predecessor_mdb_set;
	celerix::store::lmdb::component & store;

public:
	explicit block (celerix::store::lmdb::component & store_a);
	void put (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a, celerix::block const & block_a) override;
	void raw_put (store::write_transaction const & transaction_a, std::vector<uint8_t> const & data, celerix::block_hash const & hash_a) override;
	std::optional<celerix::block_hash> successor (store::transaction const & transaction_a, celerix::block_hash const & hash_a) const override;
	void successor_clear (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a) override;
	std::shared_ptr<celerix::block> get (store::transaction const & transaction_a, celerix::block_hash const & hash_a) const override;
	void del (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a) override;
	bool exists (store::transaction const & transaction_a, celerix::block_hash const & hash_a) override;
	uint64_t count (store::transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator begin (store::transaction const & transaction_a, celerix::block_hash const & hash_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;

	/**
	 * Contains block_sideband and block for all block types (legacy send/change/open/receive & state blocks)
	 * celerix::block_hash -> celerix::block_sideband, celerix::block
	 */
	MDB_dbi blocks_handle{ 0 };

protected:
	void block_raw_get (store::transaction const & transaction_a, celerix::block_hash const & hash_a, db_val & value) const;
	size_t block_successor_offset (store::transaction const & transaction_a, size_t entry_size_a, celerix::block_type type_a) const;
	static celerix::block_type block_type_from_raw (void * data_a);
};
} // namespace celerix::store::lmdb
