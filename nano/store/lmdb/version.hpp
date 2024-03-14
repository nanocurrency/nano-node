#pragma once

#include <nano/store/version.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
class version : public nano::store::version
{
protected:
	nano::store::lmdb::component & store;

public:
	explicit version (nano::store::lmdb::component & store_a);
	void put (store::write_transaction const & transaction_a, int version_a) override;
	int get (store::transaction const & transaction_a) const override;

	/**
	 * Meta information about block store, such as versions.
	 * nano::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta_handle{ 0 };
};
} // namespace nano::store::lmdb
