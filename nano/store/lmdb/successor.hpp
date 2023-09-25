#pragma once

#include <nano/store/successor.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
class component;
}
namespace nano::store::lmdb
{
class successor : public nano::store::successor
{
	friend class nano::store::lmdb::component;
	nano::store::lmdb::component & store;
	/**
		 * Maps head block to owning account
		 * nano::block_hash -> nano::block_hash
		 */
	MDB_dbi successor_v23_handle{ 0 };

public:
	successor (nano::store::lmdb::component & store);
	void put (store::write_transaction const &, nano::block_hash const &, nano::block_hash const &) override;
	nano::block_hash get (store::transaction const &, nano::block_hash const &) const override;
	void del (store::write_transaction const &, nano::block_hash const &) override;
};
} // namespace nano::store::lmdb
