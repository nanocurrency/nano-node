#pragma once

#include <celerix/store/version.hpp>

namespace celerix::store::rocksdb
{
class version : public celerix::store::version
{
protected:
	celerix::store::rocksdb::component & store;

public:
	explicit version (celerix::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, int version_a) override;
	int get (store::transaction const & transaction_a) const override;
};
} // namespace celerix::store::rocksdb
