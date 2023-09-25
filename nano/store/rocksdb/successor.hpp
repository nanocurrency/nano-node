#pragma once

#include <nano/store/successor.hpp>

namespace nano::store::rocksdb
{
class component;
}
namespace nano::store::rocksdb
{
class successor : public nano::store::successor
{
	nano::store::rocksdb::component & store;

public:
	successor (nano::store::rocksdb::component & store);
	void put (store::write_transaction const &, nano::block_hash const &, nano::block_hash const &) override;
	nano::block_hash get (store::transaction const &, nano::block_hash const &) const override;
	void del (store::write_transaction const &, nano::block_hash const &) override;
};
} // namespace nano::store::rocksdb
