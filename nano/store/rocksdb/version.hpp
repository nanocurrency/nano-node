#pragma once

#include <nano/store/version.hpp>

namespace nano::store::rocksdb
{
class version : public nano::store::version
{
protected:
	nano::store::rocksdb::component & store;

public:
	explicit version (nano::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, int version_a) override;
	int get (store::transaction const & transaction_a) const override;
};
} // namespace nano::store::rocksdb
