#pragma once

#include <celerix/store/version.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix::store::lmdb
{
class version : public celerix::store::version
{
protected:
	celerix::store::lmdb::component & store;

public:
	explicit version (celerix::store::lmdb::component & store_a);
	void put (store::write_transaction const & transaction_a, int version_a) override;
	int get (store::transaction const & transaction_a) const override;

	/**
	 * Meta information about block store, such as versions.
	 * celerix::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta_handle{ 0 };
};
} // namespace celerix::store::lmdb
